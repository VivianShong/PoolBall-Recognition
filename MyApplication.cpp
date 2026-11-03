
#include "Utilities.h"
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <limits>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <locale>

/*The cushion to cushion size of the table is 1434mm by 683mm and 
so the transformed table image has been set as 683 rows by 1434 columns.  
Each ball is 43mm in diameter.*/
const Point2f TABLE_TOP_LEFT(337, 35);
const Point2f TABLE_TOP_RIGHT(1570, 21);
const Point2f TABLE_BOTTOM_RIGHT(1888, 451);
const Point2f TABLE_BOTTOM_LEFT(38, 472);

// Labelled manually with --label flag
// Source: Media/Pool/AllBalls.jpeg (1931 x 509)
// Centers and radii are in original image pixels.

struct LabeledCircle {
    const char* label;
    cv::Point2f center;
    float radius;
};

const std::vector<LabeledCircle> labeled_circles = {
    {"9", cv::Point2f(1147.50000f, 193.500000f), 22.3999996f},
    {"White", cv::Point2f(1036.50000f, 171.500000f), 22.3999996f},
    {"2", cv::Point2f(1193.50000f, 146.500000f), 21.2999992f},
    {"1", cv::Point2f(1143.50000f, 147.500000f), 21.2999992f},
    {"8", cv::Point2f(1532.50000f, 143.500000f), 22.3999996f},
    {"4", cv::Point2f(1303.50000f, 147.500000f), 22.3999996f},
    {"13", cv::Point2f(1380.50000f, 193.500000f), 22.3999996f},
    {"12", cv::Point2f(1319.50000f, 194.500000f), 22.3999996f},
    {"5", cv::Point2f(1364.50000f, 146.500000f), 22.3999996f},
    {"6", cv::Point2f(1418.50000f, 148.500000f), 22.3999996f},
    {"7", cv::Point2f(1474.50000f, 146.500000f), 22.3999996f},
    {"10", cv::Point2f(1206.50000f, 193.500000f), 22.3999996f},
    {"3", cv::Point2f(1247.50000f, 147.500000f), 21.2999992f},
    {"14", cv::Point2f(1446.50000f, 198.500000f), 21.2999992f},
    {"15", cv::Point2f(1508.50000f, 195.500000f), 22.3999996f},
    {"11", cv::Point2f(1263.50000f, 194.500000f), 21.2999992f},
    {"net", cv::Point2f(958.500000f, 40.5000000f), 13.6000004f},
    {"net", cv::Point2f(1562.50000f, 51.5000000f), 16.8999996f},
};
namespace {
const char* LABEL_WINDOW = "AllBalls circle detection";

struct LabelSession {
    const vector<Vec3f>& circles;
    int selected = -1;
    vector<string> labels;
    double display_scale = 1.0;
    int image_height = 0;
    vector<bool> saved;
};

void SelectCircle(int event, int x, int y, int, void* userdata)
{
    if (event != EVENT_LBUTTONDOWN)
        return;
    auto& session = *static_cast<LabelSession*>(userdata);
    if (y >= session.image_height)
        return;
    const double image_x = x / session.display_scale;
    const double image_y = y / session.display_scale;
    int selected = -1;
    double nearest = numeric_limits<double>::max();
    for (size_t i = 0; i < session.circles.size(); ++i) {
        const Vec3f& ball = session.circles[i];
        const double distance = hypot(image_x - ball[0], image_y - ball[1]);
        if (distance <= ball[2] && distance < nearest) {
            selected = static_cast<int>(i);
            nearest = distance;
        }
    }
    session.selected = selected;
}

string LabelForKey(int key)
{
    if (key < 0 || key > 127)
        return "";
    key = tolower(static_cast<unsigned char>(key));
    if (key == 'w') return "White";
    if (key == 'n') return "net";
    if (key >= '1' && key <= '9') return string(1, static_cast<char>(key));
    if (key >= 'a' && key <= 'f') return to_string(10 + key - 'a');
    return "";
}

string CircleLabelText(const string& label)
{
    if (label == "White") return "w";
    if (label == "net") return "n";
    return label;
}

string SaveLabeledCircles(const Mat& image, LabelSession& session,
    const filesystem::path& output = "Media/Pool/AllBalls_labels.txt")
{
    size_t count = 0;
    try {
        if (!output.parent_path().empty())
            filesystem::create_directories(output.parent_path());
        // Replace the complete snapshot instead of appending duplicate entries.
        filesystem::path temporary = output;
        temporary += ".tmp";
        ofstream file(temporary, ios::trunc);
        file.exceptions(ios::failbit | ios::badbit);
        file.imbue(locale::classic());
        file << "// Source: Media/Pool/AllBalls.jpeg (" << image.cols << " x "
             << image.rows << ")\n"
             << "// Centers and radii are in original image pixels.\n"
             << "struct LabeledCircle {\n"
             << "    const char* label;\n"
             << "    cv::Point2f center;\n"
             << "    float radius;\n"
             << "};\n\n"
             << "const std::vector<LabeledCircle> labeled_circles = {\n";
        file << setprecision(numeric_limits<float>::max_digits10) << showpoint;
        for (size_t i = 0; i < session.circles.size(); ++i) {
            if (session.labels[i].empty())
                continue;
            const Vec3f& ball = session.circles[i];
            file << "    {\"" << session.labels[i] << "\", cv::Point2f("
                 << ball[0] << "f, " << ball[1] << "f), " << ball[2] << "f},\n";
            ++count;
        }
        file << "};\n";
        file.close();
        filesystem::rename(temporary, output);
        for (size_t i = 0; i < session.labels.size(); ++i)
            session.saved[i] = !session.labels[i].empty();
        cout << "Saved circle coordinates to " << output.string() << endl;
    } catch (const std::exception& error) {
        cerr << "Could not save circle coordinates: " << error.what() << endl;
        return "Save failed. See terminal; Enter retries.";
    }
    return "Saved " + to_string(count) + " circles; unlabeled "
        + to_string(session.circles.size() - count) + ". Enter updates the list.";
}

void LabelCircles(const Mat& image, const vector<Vec3f>& circles)
{
    if (circles.empty()) {
        cout << "No circles available to label." << endl;
        return;
    }

    LabelSession session{circles, -1, vector<string>(circles.size())};
    session.saved.assign(circles.size(), false);
    session.display_scale = min({1.0, 1100.0 / image.cols, 650.0 / image.rows});
    const int display_width = cvRound(image.cols * session.display_scale);
    session.image_height = cvRound(image.rows * session.display_scale);
    namedWindow(LABEL_WINDOW, WINDOW_AUTOSIZE);
    setMouseCallback(LABEL_WINDOW, SelectCircle, &session);
    string status = "Click circles to choose or change labels. Enter saves all labeled circles. Esc saves and exits.";
    cout << status << endl;

    while (true) {
        Mat canvas(session.image_height + 95, max(display_width, 850),
            CV_8UC3, Scalar(240, 240, 240));
        Mat image_area = canvas(Rect(0, 0, display_width, session.image_height));
        resize(image, image_area, image_area.size());
        for (size_t i = 0; i < circles.size(); ++i) {
            const Vec3f& ball = circles[i];
            const Point centre(cvRound(ball[0] * session.display_scale),
                cvRound(ball[1] * session.display_scale));
            const Scalar colour = static_cast<int>(i) == session.selected
                ? Scalar(0, 255, 255) : session.labels[i].empty()
                ? Scalar(0, 255, 0) : session.saved[i]
                ? Scalar(255, 0, 0) : Scalar(0, 165, 255);
            circle(image_area, centre, cvRound(ball[2] * session.display_scale), colour, 2);
            if (!session.labels[i].empty()) {
                const string text = CircleLabelText(session.labels[i]);
                // A dark outline keeps the label readable on any ball colour.
                putText(image_area, text, centre, FONT_HERSHEY_SIMPLEX,
                    0.65, Scalar(0, 0, 0), 4, LINE_AA);
                putText(image_area, text, centre, FONT_HERSHEY_SIMPLEX,
                    0.65, Scalar(255, 255, 255), 1, LINE_AA);
            }
        }
        putText(canvas, status, Point(12, session.image_height + 23),
            FONT_HERSHEY_SIMPLEX, 0.48, Scalar(30, 30, 30), 1, LINE_AA);
        putText(canvas, "w: white    n: net    1-9: balls 1-9    a-f: balls 10-15",
            Point(12, session.image_height + 53), FONT_HERSHEY_SIMPLEX,
            0.55, Scalar(30, 30, 30), 1, LINE_AA);
        putText(canvas, "Label keys update the preview. Enter saves all labeled circles. Esc saves and exits.",
            Point(12, session.image_height + 80), FONT_HERSHEY_SIMPLEX,
            0.5, Scalar(60, 60, 60), 1, LINE_AA);
        imshow(LABEL_WINDOW, canvas);
        const int key = waitKey(30);
        if (key == 27) {
            cout << SaveLabeledCircles(image, session) << endl;
            break;
        }
        if (getWindowProperty(LABEL_WINDOW, WND_PROP_VISIBLE) < 1)
            break;
        if (key == 10 || key == 13) {
            status = SaveLabeledCircles(image, session);
            cout << status << endl;
            continue;
        }
        const string key_label = LabelForKey(key);
        if (session.selected < 0 || key_label.empty())
            continue;
        session.labels[session.selected] = key_label;
        session.saved[session.selected] = false;
        status = "Preview: " + key_label + ". Enter saves all labeled circles.";
    }
    if (getWindowProperty(LABEL_WINDOW, WND_PROP_VISIBLE) >= 1)
        setMouseCallback(LABEL_WINDOW, nullptr, nullptr);
}
} // namespace

void MyApplication(bool label_mode)
{
	char* file_location = "Media/Pool/";
	char* pool_files[] = {
		"AllBalls.jpeg",
		"Table1.jpeg",
		"Table2.jpeg",
		"Table3.jpeg",
		"Table4.jpeg",
		"Table5.jpeg",
		"Table6.jpeg"
	};

	// Load images
	int number_of_images = sizeof(pool_files) / sizeof(pool_files[0]);
	Mat* pool_image = new Mat[number_of_images];
	//Mat* new_pool_image = new Mat[number_of_images];
	for (int file_no = 0; (file_no < number_of_images); file_no++)
	{
		cout << pool_files[file_no] << endl;
		string filename(file_location);
		filename.append(pool_files[file_no]);
		pool_image[file_no] = imread(filename, -1);

		if (pool_image[file_no].empty())
		{
			cout << "Could not open " << pool_image[file_no] << endl;
		}
		imshow(pool_files[file_no], pool_image[file_no]);

		char pause = cv::waitKey();  // This introduces a delay between frame.  The length of the delay/number can be reduced.
	}
	// Find circles in the sample image to allow manual labeling
	if (!pool_image[0].empty())
	{
		Mat grey;
		cvtColor(pool_image[0], grey, COLOR_BGR2GRAY);

		vector<Vec3f> circles;
		HoughCircles(grey, circles, HOUGH_GRADIENT, 1.0, 21.5,
			100.0, 20.0, 10, 23);

		Mat detected_balls = pool_image[0].clone();
		const vector<Point2f> table_border = {
			TABLE_TOP_LEFT, TABLE_TOP_RIGHT, TABLE_BOTTOM_RIGHT, TABLE_BOTTOM_LEFT
		};
		// Keep centers inside the table or exactly on its border.
		circles.erase(remove_if(circles.begin(), circles.end(),
			[&table_border](const Vec3f& ball) {
				return pointPolygonTest(table_border, Point2f(ball[0], ball[1]), false) < 0;
			}), circles.end());

		int count = 0;
		for (const Vec3f& ball : circles)
		{
			Point centre(cvRound(ball[0]), cvRound(ball[1]));
			circle(detected_balls, centre, cvRound(ball[2]), Scalar(0, 255, 0), 2);
			circle(detected_balls, centre, 2, Scalar(0, 0, 255), FILLED);
			count += 1;
		}
		
		imshow("AllBalls circle detection", detected_balls);
		cout << "Detected " << count << " circles." << endl;
		if (label_mode)
			LabelCircles(pool_image[0], circles);
		else
			waitKey();
	}
	

}

