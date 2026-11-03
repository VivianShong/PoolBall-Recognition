
#include "Utilities.h"
#include <filesystem>
#include <fstream>
#include <locale>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cctype>
#include <iomanip>
#include <array>
#include <stdexcept>

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
    std::string label;
    cv::Point2f center;
    float radius;
};

std::vector<LabeledCircle> labeled_circles = {
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

const Size TRANSFORMED_TABLE_SIZE(1434, 683);
constexpr float BALL_RADIUS = 21.5f;
constexpr double MATCH_DISTANCE = BALL_RADIUS / 2.0; // 10.75 transformed pixels.
constexpr int BALL_CLASSES = 16; // 1-15, White.
constexpr int BACKGROUND = BALL_CLASSES;

struct BallPrediction {
    Point2f center; // Always in transformed image coordinates.
    string label;
};

// Same grayscale conversion, Hough settings and table filter as AllBalls.
// Circle coordinates and radii are in the original camera image.
vector<Vec3f> HoughDetector(const Mat& image)
{
    Mat grey;
    cvtColor(image, grey, COLOR_BGR2GRAY);
    vector<Vec3f> circles;
    HoughCircles(grey, circles, HOUGH_GRADIENT, 1.0, 21.5,
        100.0, 20.0, 10, 23);
    const vector<Point2f> table_border = {
        TABLE_TOP_LEFT, TABLE_TOP_RIGHT, TABLE_BOTTOM_RIGHT, TABLE_BOTTOM_LEFT
    };
    circles.erase(remove_if(circles.begin(), circles.end(),
        [&table_border](const Vec3f& ball) {
            return pointPolygonTest(table_border, Point2f(ball[0], ball[1]), false) < 0;
        }), circles.end());
    return circles;
}
// TODO: replace with our logic
string Classifier(const Mat&, const Vec3f&)
{
    return "White";
}

Mat TablePerspectiveTransform()
{
    const vector<Point2f> camera = {
        TABLE_TOP_LEFT, TABLE_TOP_RIGHT, TABLE_BOTTOM_RIGHT, TABLE_BOTTOM_LEFT
    };
    const vector<Point2f> table = {
        Point2f(0, 0), Point2f(1433, 0), Point2f(1433, 682), Point2f(0, 682)
    };
    return getPerspectiveTransform(camera, table);
}

struct DetectedCircle {
    Vec3f image_circle; // Original-image x, y, radius for histograms and drawing.
    Point2f table_center; // Perspective-transformed center for evaluation.
};

// Detection owns the coordinate conversion; evaluation receives table coordinates.
vector<DetectedCircle> Detector(const Mat& image)
{
    const vector<Vec3f> circles = HoughDetector(image);
    vector<Point2f> centers, transformed_centers;
    for (const Vec3f& circle : circles)
        centers.emplace_back(circle[0], circle[1]);
    if (!centers.empty())
        perspectiveTransform(centers, transformed_centers, TablePerspectiveTransform());
    vector<DetectedCircle> detections;
    for (size_t i = 0; i < circles.size(); ++i)
        detections.push_back({circles[i], transformed_centers[i]});
    return detections;
}

string BallClassName(int index)
{
    if (index == BACKGROUND) return "BG";
    return index == 15 ? "White" : to_string(index + 1);
}

int BallClassIndex(const string& label)
{
    for (int index = 0; index < BALL_CLASSES; ++index)
        if (label == BallClassName(index)) return index;
    throw invalid_argument("Unsupported evaluation ball label: " + label);
}

struct GroundTruthImage {
    const char* filename;
    array<Point2f, BALL_CLASSES> centers; // Index 0-14: balls 1-15; index 15: White.
};

const vector<GroundTruthImage>& SuppliedGroundTruth()
{
    // Snapshot of "Ball Locations In Transformed Image (1).xlsx", Sheet1, rows 4-9.
    // Workbook Row,Column pairs become Point2f(Column,Row). Blank pairs are absent.
    // AllBalls is training data and is deliberately excluded from test totals.
    // Values are preserved as supplied; out-of-bounds entries are flagged below.
    static const vector<GroundTruthImage> truth = {
        {"Table1.jpeg", {{
            Point2f(1092.0f, 309.0f), Point2f(429.0f, 1212.0f), Point2f(1174.0f, 353.0f), Point2f(335.0f, 1050.0f),
            Point2f(1211.0f, 285.0f), Point2f(1170.0f, 261.0f), Point2f(1173.0f, 352.0f), Point2f(1132.0f, 331.0f),
            Point2f(1210.0f, 240.0f), Point2f(1170.0f, 310.0f), Point2f(407.0f, 1169.0f), Point2f(1135.0f, 281.0f),
            Point2f(1212.0f, 374.0f), Point2f(1091.0f, 360.0f), Point2f(1213.0f, 331.0f), Point2f(29.0f, 288.0f)
        }}},
        {"Table2.jpeg", {{
            Point2f(1103.0f, 282.0f), Point2f(1224.0f, 620.0f), Point2f(1121.0f, 391.0f), Point2f(1305.0f, 310.0f),
            Point2f(1046.0f, 315.0f), Point2f(621.0f, 202.0f), Point2f(1173.0f, 361.0f), Point2f(1132.0f, 331.0f),
            Point2f(725.0f, 177.0f), Point2f(1269.0f, 185.0f), Point2f(798.0f, 665.0f), Point2f(438.0f, 296.0f),
            Point2f(1235.0f, 53.0f), Point2f(1090.0f, 362.0f), Point2f(1205.0f, 399.0f), Point2f(1169.0f, 507.0f)
        }}},
        {"Table3.jpeg", {{
            Point2f(908.0f, 23.0f), Point2f(1224.0f, 616.0f), Point2f(1199.0f, 447.0f), Point2f(29.0f, 336.0f),
            Point2f(1406.0f, 305.0f), Point2f(621.0f, 210.0f), Point2f(1294.0f, 260.0f), Point2f(1252.0f, 226.0f),
            Point2f(725.0f, 177.0f), Point2f(1270.0f, 171.0f), Point2f(798.0f, 665.0f), Point2f(351.0f, 249.0f),
            Point2f(1233.0f, 53.0f), Point2f(901.0f, 169.0f), Point2f(1201.0f, 399.0f), Point2f(29.0f, 434.0f)
        }}},
        {"Table4.jpeg", {{
            Point2f(908.0f, 23.0f), Point2f(1407.0f, 546.0f), Point2f(1204.0f, 444.0f), Point2f(-1, -1),
            Point2f(1406.0f, 305.0f), Point2f(621.0f, 210.0f), Point2f(1294.0f, 260.0f), Point2f(1252.0f, 226.0f),
            Point2f(724.0f, 181.0f), Point2f(1270.0f, 171.0f), Point2f(798.0f, 665.0f), Point2f(351.0f, 249.0f),
            Point2f(1231.0f, 58.0f), Point2f(901.0f, 169.0f), Point2f(1201.0f, 399.0f), Point2f(1392.0f, 88.0f)
        }}},
        {"Table5.jpeg", {{
            Point2f(23.0f, 535.0f), Point2f(1407.0f, 546.0f), Point2f(1204.0f, 444.0f), Point2f(-1, -1),
            Point2f(1406.0f, 305.0f), Point2f(25.0f, 131.0f), Point2f(1294.0f, 260.0f), Point2f(1252.0f, 226.0f),
            Point2f(72.0f, 538.0f), Point2f(1270.0f, 171.0f), Point2f(798.0f, 665.0f), Point2f(351.0f, 249.0f),
            Point2f(-1, -1), Point2f(901.0f, 169.0f), Point2f(1201.0f, 399.0f), Point2f(26.0f, 608.0f)
        }}},
        {"Table6.jpeg", {{
            Point2f(23.0f, 535.0f), Point2f(1364.0f, 477.0f), Point2f(1204.0f, 444.0f), Point2f(-1, -1),
            Point2f(1406.0f, 305.0f), Point2f(25.0f, 131.0f), Point2f(1294.0f, 260.0f), Point2f(1252.0f, 226.0f),
            Point2f(72.0f, 538.0f), Point2f(1270.0f, 171.0f), Point2f(-1, -1), Point2f(351.0f, 249.0f),
            Point2f(-1, -1), Point2f(901.0f, 169.0f), Point2f(1201.0f, 399.0f), Point2f(947.0f, 629.0f)
        }}},
    };
    return truth;
}

// Rectangular Hungarian assignment: maximize the number of matches within the
// threshold, then minimize their total distance. Labels do not affect matching.
vector<int> MatchCenters(const vector<BallPrediction>& predictions,
    const vector<BallPrediction>& truth)
{
    const int rows = static_cast<int>(truth.size());
    const int predictions_count = static_cast<int>(predictions.size());
    const int columns = predictions_count + rows; // One unmatched slot per GT.
    const double unmatched = (rows + 1) * MATCH_DISTANCE;
    const double invalid = (rows + 1) * unmatched;
    vector<vector<double>> cost(rows + 1, vector<double>(columns + 1, unmatched));
    for (int i = 1; i <= rows; ++i)
        for (int j = 1; j <= predictions_count; ++j) {
            const double distance = norm(truth[i - 1].center - predictions[j - 1].center);
            cost[i][j] = isfinite(distance) && distance <= MATCH_DISTANCE ? distance : invalid;
        }
    vector<double> u(rows + 1), v(columns + 1);
    vector<int> assigned_row(columns + 1), previous(columns + 1);
    for (int i = 1; i <= rows; ++i) {
        assigned_row[0] = i;
        int column = 0;
        vector<double> minimum(columns + 1, numeric_limits<double>::infinity());
        vector<bool> used(columns + 1, false);
        do {
            used[column] = true;
            const int row = assigned_row[column];
            double delta = numeric_limits<double>::infinity();
            int next = 0;
            for (int j = 1; j <= columns; ++j) {
                if (used[j]) continue;
                const double reduced = cost[row][j] - u[row] - v[j];
                if (reduced < minimum[j]) {
                    minimum[j] = reduced;
                    previous[j] = column;
                }
                if (minimum[j] < delta) {
                    delta = minimum[j];
                    next = j;
                }
            }
            for (int j = 0; j <= columns; ++j) {
                if (used[j]) {
                    u[assigned_row[j]] += delta;
                    v[j] -= delta;
                } else minimum[j] -= delta;
            }
            column = next;
        } while (assigned_row[column] != 0);
        do {
            const int next = previous[column];
            assigned_row[column] = assigned_row[next];
            column = next;
        } while (column != 0);
    }
    vector<int> matches(rows, -1);
    for (int j = 1; j <= predictions_count; ++j)
        if (assigned_row[j] != 0 && cost[assigned_row[j]][j] <= MATCH_DISTANCE)
            matches[assigned_row[j] - 1] = j - 1;
    return matches;
}

struct EvaluationResult {
    size_t tp = 0, fp = 0, fn = 0, correct_labels = 0;
    double squared_center_error = 0;
    array<array<size_t, BALL_CLASSES + 1>, BALL_CLASSES + 1> confusion{};
};

EvaluationResult Evaluation(const vector<BallPrediction>& predictions,
    const vector<BallPrediction>& truth)
{
    EvaluationResult result;
    // Validate all labels, including unmatched predictions.
    for (const auto& prediction : predictions) BallClassIndex(prediction.label);
    const vector<int> matches = MatchCenters(predictions, truth);
    vector<bool> used(predictions.size(), false);
    for (size_t i = 0; i < truth.size(); ++i) {
        const int actual = BallClassIndex(truth[i].label);
        const int match = matches[i];
        if (match < 0) {
            ++result.fn;
            ++result.confusion[actual][BACKGROUND]; // Missed ball.
            continue;
        }
        used[match] = true;
        ++result.tp;
        const int predicted = BallClassIndex(predictions[match].label);
        ++result.confusion[actual][predicted];
        if (actual == predicted) ++result.correct_labels;
        const Point2f difference = truth[i].center - predictions[match].center;
        result.squared_center_error += static_cast<double>(difference.x) * difference.x
            + static_cast<double>(difference.y) * difference.y;
    }
    for (size_t i = 0; i < predictions.size(); ++i)
        if (!used[i]) {
            ++result.fp;
            ++result.confusion[BACKGROUND][BallClassIndex(predictions[i].label)];
        }
    return result;
}

void AddEvaluation(EvaluationResult& total, const EvaluationResult& image)
{
    total.tp += image.tp;
    total.fp += image.fp;
    total.fn += image.fn;
    total.correct_labels += image.correct_labels;
    total.squared_center_error += image.squared_center_error;
    for (int i = 0; i <= BALL_CLASSES; ++i)
        for (int j = 0; j <= BALL_CLASSES; ++j)
            total.confusion[i][j] += image.confusion[i][j];
}

void PrintRatio(const char* name, double numerator, size_t denominator)
{
    cout << "  " << name << ": ";
    if (denominator == 0) cout << "N/A\n";
    else cout << numerator / denominator << '\n';
}

void PrintEvaluation(const string& name, const EvaluationResult& result)
{
    cout << '\n' << name << ": TP=" << result.tp << " FP=" << result.fp
         << " FN=" << result.fn << '\n';
    PrintRatio("Precision", result.tp, result.tp + result.fp);
    PrintRatio("Recall", result.tp, result.tp + result.fn);
    PrintRatio("F1", 2.0 * result.tp, 2 * result.tp + result.fp + result.fn);
    PrintRatio("Classification accuracy (matched balls)", result.correct_labels, result.tp);
    PrintRatio("Correctly identified recall", result.correct_labels, result.tp + result.fn);
    PrintRatio("Center MSE (pixels^2, matched balls)", result.squared_center_error, result.tp);
    cout << "  Center RMSE (pixels, matched balls): ";
    if (result.tp == 0) cout << "N/A\n";
    else cout << sqrt(result.squared_center_error / result.tp) << '\n';
}


struct ImageEvaluation {
    string filename;
    EvaluationResult result;
    size_t excluded_gt;
};

struct ExcludedGroundTruth {
    string filename, label;
    Point2f center;
};

void WriteCsvField(ostream& file, const string& value)
{
    file << '"';
    for (char character : value) {
        if (character == '"') file << '"';
        file << character;
    }
    file << '"';
}

void WriteEvaluationRow(ostream& file, const string& name,
    const EvaluationResult& result, size_t excluded, size_t images)
{
    WriteCsvField(file, name);
    file << ',' << result.tp << ',' << result.fp << ',' << result.fn
         << ',' << result.tp + result.fn << ',' << result.tp + result.fp
         << ',' << result.correct_labels;
    const auto ratio = [&file](double numerator, size_t denominator) {
        file << ',';
        if (denominator == 0) file << "N/A";
        else file << numerator / denominator;
    };
    ratio(result.tp, result.tp + result.fp);
    ratio(result.tp, result.tp + result.fn);
    ratio(2.0 * result.tp, 2 * result.tp + result.fp + result.fn);
    ratio(result.correct_labels, result.tp);
    ratio(result.correct_labels, result.tp + result.fn);
    ratio(result.squared_center_error, result.tp);
    file << ',';
    if (result.tp == 0) file << "N/A";
    else file << sqrt(result.squared_center_error / result.tp);
    file << ',' << excluded << ',' << images << ',' << MATCH_DISTANCE << '\n';
}

void WriteEvaluationCsv(const vector<ImageEvaluation>& images,
    const EvaluationResult& total, const vector<ExcludedGroundTruth>& exclusions,
    const filesystem::path& directory = "output/evaluation")
{
    try {
        filesystem::create_directories(directory);
        const auto open_csv = [&directory](const char* filename) {
            ofstream file;
            file.exceptions(ios::failbit | ios::badbit);
            file.open(directory / filename, ios::trunc);
            file.imbue(locale::classic());
            file << setprecision(10);
            return file;
        };
        auto results = open_csv("results.csv");
        results << "image,tp,fp,fn,ground_truth_count,prediction_count,correct_labels,"
                << "precision,recall,f1,classification_accuracy,correctly_identified_recall,"
                << "center_mse_pixels_squared,center_rmse_pixels,excluded_ground_truth,"
                << "evaluated_images,match_distance_pixels\n";
        for (const auto& image : images)
            WriteEvaluationRow(results, image.filename, image.result, image.excluded_gt, 1);
        WriteEvaluationRow(results, "Pooled total", total, exclusions.size(), images.size());
        results.close();

        auto confusion = open_csv("confusion_matrix.csv");
        confusion << "ground_truth/prediction";
        for (int j = 0; j <= BALL_CLASSES; ++j) confusion << ',' << BallClassName(j);
        confusion << '\n';
        for (int i = 0; i <= BALL_CLASSES; ++i) {
            confusion << BallClassName(i);
            for (int j = 0; j <= BALL_CLASSES; ++j) confusion << ',' << total.confusion[i][j];
            confusion << '\n';
        }
        confusion.close();

        auto excluded = open_csv("excluded_ground_truth.csv");
        excluded << "image,label,x,y,reason\n";
        for (const auto& entry : exclusions) {
            WriteCsvField(excluded, entry.filename);
            excluded << ',';
            WriteCsvField(excluded, entry.label);
            excluded << ',' << entry.center.x << ',' << entry.center.y
                     << ",invalid_transformed_coordinates\n";
        }
        excluded.close();
        cout << "CSV results written to " << (directory / "results.csv").string() << '\n';
    } catch (const std::exception& error) {
        cerr << "Could not write evaluation CSV files: " << error.what() << '\n';
    }
}

Mat DrawDetectedCircles(const Mat& image, const vector<Vec3f>& circles,
    const string& filename)
{
    Mat annotated = image.clone();
    for (const Vec3f& ball : circles) {
        const Point center(cvRound(ball[0]), cvRound(ball[1]));
        circle(annotated, center, cvRound(ball[2]), Scalar(0, 255, 0), 2);
        circle(annotated, center, 2, Scalar(0, 0, 255), FILLED);
    }
    const double scale = min({1.0, 1100.0 / image.cols, 650.0 / image.rows});
    Mat preview;
    resize(annotated, preview, Size(cvRound(image.cols * scale), cvRound(image.rows * scale)));
    copyMakeBorder(preview, preview, 60, 0, 0, 0, BORDER_CONSTANT, Scalar(30, 30, 30));
    putText(preview, filename + " - " + to_string(circles.size()) + " circles",
        Point(12, 23), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(255, 255, 255), 1, LINE_AA);
    putText(preview, "Press any key for next image; Esc stops evaluation.",
        Point(12, 47), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255, 255, 255), 1, LINE_AA);
    return preview;
}

void RunEvaluation(bool visualize = true)
{
    cout << "\nHough detection + dummy White classification: Table1-6; AllBalls excluded as training data.\n"
         << "Transformed image: 1434x683. Match distance <= " << MATCH_DISTANCE
         << " pixels (half of the 21.5-pixel ball radius).\n"
         << "Ground truth is provisional; invalid coordinates are excluded, not corrected.\n";
    EvaluationResult total;
    size_t excluded = 0, evaluated_images = 0;
    vector<ImageEvaluation> image_results;
    vector<ExcludedGroundTruth> exclusions;
    for (const auto& supplied : SuppliedGroundTruth()) {
        const Mat source = imread(string("Media/Pool/") + supplied.filename);
        if (source.empty()) {
            cerr << "Cannot evaluate missing image: " << supplied.filename << '\n';
            continue;
        }
        const size_t excluded_before = excluded;
        vector<BallPrediction> truth;
        for (int i = 0; i < BALL_CLASSES; ++i) {
            const Point2f center = supplied.centers[i];
            if (center == Point2f(-1, -1)) continue;
            if (!isfinite(center.x) || !isfinite(center.y) || center.x < 0 || center.y < 0
                || center.x >= TRANSFORMED_TABLE_SIZE.width || center.y >= TRANSFORMED_TABLE_SIZE.height) {
                ++excluded;
                exclusions.push_back({supplied.filename, BallClassName(i), center});
                cerr << "Excluded ground truth: " << supplied.filename << ", ball "
                     << BallClassName(i) << ", (x,y)=(" << center.x << ',' << center.y << ")\n";
                continue;
            }
            truth.push_back({center, BallClassName(i)});
        }
        const vector<DetectedCircle> detections = Detector(source);
        vector<Vec3f> circles;
        vector<BallPrediction> predictions;
        for (const DetectedCircle& detection : detections) {
            circles.push_back(detection.image_circle);
            predictions.push_back({detection.table_center,
                Classifier(source, detection.image_circle)});
        }
        const EvaluationResult result = Evaluation(predictions, truth);
        PrintEvaluation(supplied.filename, result);
        image_results.push_back({supplied.filename, result, excluded - excluded_before});
        AddEvaluation(total, result);
        ++evaluated_images;
        if (visualize) {
            imshow("Test image circle detection", DrawDetectedCircles(source, circles, supplied.filename));
            if (waitKey(0) == 27)
                break;
        }
    }
    if (visualize && evaluated_images > 0)
        destroyWindow("Test image circle detection");
    PrintEvaluation("Pooled total", total);
    cout << "Evaluated images: " << evaluated_images << "/6; excluded GT entries: " << excluded << '\n';
    cout << "\nConfusion matrix: rows=ground truth, columns=prediction.\n"
         << "BG column=missed ball; BG row=unmatched prediction; BG/BG is unused.\n";
    cout << setw(7) << "GT/Pred";
    for (int j = 0; j <= BALL_CLASSES; ++j) cout << setw(6) << BallClassName(j);
    cout << '\n';
    for (int i = 0; i <= BALL_CLASSES; ++i) {
        cout << setw(7) << BallClassName(i);
        for (int j = 0; j <= BALL_CLASSES; ++j) cout << setw(6) << total.confusion[i][j];
        cout << '\n';
    }
    WriteEvaluationCsv(image_results, total, exclusions);
}

const char* LABEL_WINDOW = "AllBalls circle detection";

struct LabelSession {
    const vector<Vec3f>& circles;
    int selected = -1;
    vector<string> labels;
    double display_scale = 1.0;
    int image_height = 0;
    vector<bool> applied;
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

string UpdateLabeledCircles(LabelSession& session)
{
    vector<LabeledCircle> updated;
    updated.reserve(session.circles.size());
    for (size_t i = 0; i < session.circles.size(); ++i) {
        if (session.labels[i].empty())
            continue;
        const Vec3f& ball = session.circles[i];
        updated.push_back({session.labels[i], Point2f(ball[0], ball[1]), ball[2]});
    }
    labeled_circles.swap(updated);
    for (size_t i = 0; i < session.labels.size(); ++i)
        session.applied[i] = !session.labels[i].empty();
    return "Updated " + to_string(labeled_circles.size()) + " circles in memory; unlabeled "
        + to_string(session.circles.size() - labeled_circles.size()) + ". Enter updates the list.";
}

void LabelCircles(const Mat& image, const vector<Vec3f>& circles)
{
    if (circles.empty()) {
        cout << "No circles available to label." << endl;
        return;
    }

    LabelSession session{circles, -1, vector<string>(circles.size())};
    session.applied.assign(circles.size(), false);
    session.display_scale = min({1.0, 1100.0 / image.cols, 650.0 / image.rows});
    const int display_width = cvRound(image.cols * session.display_scale);
    session.image_height = cvRound(image.rows * session.display_scale);
    namedWindow(LABEL_WINDOW, WINDOW_AUTOSIZE);
    setMouseCallback(LABEL_WINDOW, SelectCircle, &session);
    string status = "Click circles to choose or change labels. Enter updates labeled_circles. Esc updates and exits.";
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
                ? Scalar(0, 255, 0) : session.applied[i]
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
        putText(canvas, "Label keys update the preview. Enter updates labeled_circles. Esc updates and exits.",
            Point(12, session.image_height + 80), FONT_HERSHEY_SIMPLEX,
            0.5, Scalar(60, 60, 60), 1, LINE_AA);
        imshow(LABEL_WINDOW, canvas);
        const int key = waitKey(30);
        if (key == 27) {
            cout << UpdateLabeledCircles(session) << endl;
            break;
        }
        if (getWindowProperty(LABEL_WINDOW, WND_PROP_VISIBLE) < 1)
            break;
        if (key == 10 || key == 13) {
            status = UpdateLabeledCircles(session);
            cout << status << endl;
            continue;
        }
        const string key_label = LabelForKey(key);
        if (session.selected < 0 || key_label.empty())
            continue;
        session.labels[session.selected] = key_label;
        session.applied[session.selected] = false;
        status = "Preview: " + key_label + ". Enter updates labeled_circles.";
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
        const vector<Vec3f> circles = HoughDetector(pool_image[0]);
        Mat detected_balls = pool_image[0].clone();

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
		else{
			RunEvaluation();
			waitKey();
		}
	}
	

}

