
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
#include <sstream>

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
    {"1", cv::Point2f(1143.50000f, 147.500000f), 21.1000004f},
    {"2", cv::Point2f(1193.50000f, 146.500000f), 21.6000004f},
    {"3", cv::Point2f(1247.50000f, 147.500000f), 21.3999996f},
    {"4", cv::Point2f(1302.50000f, 146.500000f), 21.6000004f},
    {"5", cv::Point2f(1364.50000f, 146.500000f), 21.7999992f},
    {"6", cv::Point2f(1419.50000f, 148.500000f), 21.0000000f},
    {"7", cv::Point2f(1473.50000f, 146.500000f), 22.2000008f},
    {"8", cv::Point2f(1532.50000f, 142.500000f), 22.1000004f},
    {"9", cv::Point2f(1147.50000f, 193.500000f), 22.1000004f},
    {"10", cv::Point2f(1205.50000f, 192.500000f), 22.2000008f},
    {"11", cv::Point2f(1261.50000f, 193.500000f), 22.2000008f},
    {"12", cv::Point2f(1315.50000f, 196.500000f), 24.3999996f},
    {"13", cv::Point2f(1381.50000f, 194.500000f), 23.2999992f},
    {"14", cv::Point2f(1444.50000f, 195.500000f), 23.2999992f},
    {"15", cv::Point2f(1508.50000f, 197.500000f), 21.1000004f},
    {"White", cv::Point2f(1036.50000f, 171.500000f), 21.7999992f},
    {"net", cv::Point2f(963.500000f, 30.5000000f), 21.0000000f},
    {"table", cv::Point2f(951.456360f, 442.374542f), 47.4297943f},
    {"table", cv::Point2f(394.977264f, 115.860001f), 41.4302216f},
    {"table", cv::Point2f(103.571815f, 426.575439f), 38.5401459f},
    {"table", cv::Point2f(1213.01904f, 89.5281830f), 33.5379486f},
    {"table", cv::Point2f(1827.42822f, 417.798187f), 41.4301758f},
};
namespace {

const Size TRANSFORMED_TABLE_SIZE(1434, 683);
constexpr float BALL_RADIUS = 21.5f;
constexpr double MATCH_DISTANCE = BALL_RADIUS; // 21.5 transformed pixels.
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
        100.0, 12.0, 19, 25);
    const vector<Point2f> table_border = {
        TABLE_TOP_LEFT, TABLE_TOP_RIGHT, TABLE_BOTTOM_RIGHT, TABLE_BOTTOM_LEFT
    };
    circles.erase(remove_if(circles.begin(), circles.end(),
        [&table_border](const Vec3f& ball) {
            return pointPolygonTest(table_border, Point2f(ball[0], ball[1]), false) < 0;
        }), circles.end());
    return circles;
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


struct HistogramReference {
    string label;
    Mat histogram;
};

// H, L, S bins and ranges for OpenCV's 8-bit HLS representation.
Mat CalculateHLSHistogram(const Mat& hls, const Vec3f& circle)
{
    if (hls.empty() || !isfinite(circle[0]) || !isfinite(circle[1])
        || !isfinite(circle[2]) || circle[2] <= 0)
        return Mat();
    const int left = max(0, static_cast<int>(floor(circle[0] - circle[2])));
    const int top = max(0, static_cast<int>(floor(circle[1] - circle[2])));
    const int right = min(hls.cols, static_cast<int>(ceil(circle[0] + circle[2])) + 1);
    const int bottom = min(hls.rows, static_cast<int>(ceil(circle[1] + circle[2])) + 1);
    if (left >= right || top >= bottom) return Mat();
    const Rect region(left, top, right - left, bottom - top);
    const Mat patch = hls(region);
    Mat mask(region.size(), CV_8UC1, Scalar(0));
    const double radius_squared = static_cast<double>(circle[2]) * circle[2];
    for (int y = 0; y < mask.rows; ++y) {
        uchar* row = mask.ptr<uchar>(y);
        for (int x = 0; x < mask.cols; ++x) {
            const double dx = x + left - circle[0];
            const double dy = y + top - circle[1];
            if (dx * dx + dy * dy <= radius_squared) row[x] = 255;
        }
    }
    if (countNonZero(mask) == 0) return Mat();
    const int channels[] = {0, 1, 2};
    const int bins[] = {18, 8, 8};
    const float hue[] = {0, 180}, lightness[] = {0, 256}, saturation[] = {0, 256};
    const float* ranges[] = {hue, lightness, saturation};
    Mat histogram;
    calcHist(&patch, 1, channels, mask, histogram, 3, bins, ranges);
    normalize(histogram, histogram, 1.0, 0.0, NORM_L1);
    return histogram;
}

vector<HistogramReference> BuildReferenceHistograms(const Mat& all_balls,
    const vector<LabeledCircle>& samples)
{
    if (all_balls.empty()) throw invalid_argument("AllBalls training image is empty.");
    Mat hls;
    cvtColor(all_balls, hls, COLOR_BGR2HLS);
    vector<HistogramReference> references;
    bool has_background = false;
    for (const auto& sample : samples) {
        const bool background = sample.label == "net" || sample.label == "table";
        if (!background) BallClassIndex(sample.label);
        Mat histogram = CalculateHLSHistogram(hls,
            Vec3f(sample.center.x, sample.center.y, sample.radius));
        if (histogram.empty())
            throw invalid_argument("Reference has no image pixels: " + sample.label);
        references.push_back({sample.label, histogram});
        has_background = has_background || background;
    }
    if (!has_background)
        throw invalid_argument("Classification requires a labeled net or table reference.");
    return references;
}

// Minimum-cost one-to-one assignment of rows to columns (columns >= rows).
// Unlike evaluation matching, these costs describe appearance, not position.
vector<int> MinimumCostAssignment(const vector<vector<double>>& costs)
{
    const int rows = static_cast<int>(costs.size());
    if (rows == 0) return {};
    const int columns = static_cast<int>(costs[0].size());
    if (columns < rows) throw invalid_argument("Assignment needs at least one column per row.");
    for (const auto& row : costs) {
        if (static_cast<int>(row.size()) != columns)
            throw invalid_argument("Assignment cost matrix must be rectangular.");
        for (double cost : row)
            if (!isfinite(cost)) throw invalid_argument("Assignment costs must be finite.");
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
                const double reduced = costs[row - 1][j - 1] - u[row] - v[j];
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
    vector<int> assignment(rows, -1);
    for (int j = 1; j <= columns; ++j)
        if (assigned_row[j] != 0) assignment[assigned_row[j] - 1] = j - 1;
    return assignment;
}

struct ClassifiedCircle {
    DetectedCircle detection;
    string label;
    double histogram_distance;
};

vector<ClassifiedCircle> Classifier(const Mat& image,
    const vector<DetectedCircle>& detections, const vector<HistogramReference>& references)
{
    if (detections.empty()) return {};
    if (references.empty()) throw invalid_argument("Precompute reference histograms first.");
    bool has_background = false;
    for (const auto& reference : references)
        has_background = has_background || reference.label == "net" || reference.label == "table";
    if (!has_background) throw invalid_argument("No net/table rejection references.");
    Mat hls;
    cvtColor(image, hls, COLOR_BGR2HLS);
    // Bhattacharyya distance is in [0,1]; 2 marks an unavailable ball assignment.
    vector<vector<double>> costs(detections.size(),
        vector<double>(BALL_CLASSES + detections.size(), 2.0));
    for (size_t i = 0; i < detections.size(); ++i) {
        const Mat histogram = CalculateHLSHistogram(hls, detections[i].image_circle);
        double rejection = histogram.empty() ? 0.0 : 2.0;
        if (!histogram.empty()) {
            for (const auto& reference : references) {
                const double distance = compareHist(histogram, reference.histogram, HISTCMP_BHATTACHARYYA);
                if (reference.label == "net" || reference.label == "table")
                    rejection = min(rejection, distance);
                else {
                    const int label = BallClassIndex(reference.label);
                    costs[i][label] = min(costs[i][label], distance);
                }
            }
        }
        // There is one rejection slot per candidate, so any number can be rejected.
        for (size_t j = BALL_CLASSES; j < costs[i].size(); ++j)
            costs[i][j] = rejection;
        // Prefer rejection on ties. A worse-than-background match cannot improve
        // the global solution because rejection slots never run out.
        for (int label = 0; label < BALL_CLASSES; ++label)
            if (costs[i][label] >= rejection) costs[i][label] = 2.0;
    }
    const vector<int> assignment = MinimumCostAssignment(costs);
    vector<ClassifiedCircle> retained;
    for (size_t i = 0; i < detections.size(); ++i) {
        const int label = assignment[i];
        if (label >= 0 && label < BALL_CLASSES)
            retained.push_back({detections[i], BallClassName(label), costs[i][label]});
    }
    return retained;
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
    const vector<BallPrediction>& truth, bool evaluate_labels = true)
{
    EvaluationResult result;
    // Validate all labels, including unmatched predictions.
    if (evaluate_labels)
        for (const auto& prediction : predictions) BallClassIndex(prediction.label);
    const vector<int> matches = MatchCenters(predictions, truth);
    vector<bool> used(predictions.size(), false);
    for (size_t i = 0; i < truth.size(); ++i) {
        const int actual = evaluate_labels ? BallClassIndex(truth[i].label) : BACKGROUND;
        const int match = matches[i];
        if (match < 0) {
            ++result.fn;
            if (evaluate_labels) ++result.confusion[actual][BACKGROUND]; // Missed ball.
            continue;
        }
        used[match] = true;
        ++result.tp;
        if (evaluate_labels) {
            const int predicted = BallClassIndex(predictions[match].label);
            ++result.confusion[actual][predicted];
            if (actual == predicted) ++result.correct_labels;
        }
        const Point2f difference = truth[i].center - predictions[match].center;
        result.squared_center_error += static_cast<double>(difference.x) * difference.x
            + static_cast<double>(difference.y) * difference.y;
    }
    for (size_t i = 0; i < predictions.size(); ++i)
        if (!used[i]) {
            ++result.fp;
            if (evaluate_labels)
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

void PrintEvaluation(const string& name, const EvaluationResult& result, bool evaluate_labels = true)
{
    cout << '\n' << name << ": TP=" << result.tp << " FP=" << result.fp
         << " FN=" << result.fn << '\n';
    PrintRatio("Precision", result.tp, result.tp + result.fp);
    PrintRatio("Recall", result.tp, result.tp + result.fn);
    PrintRatio("F1", 2.0 * result.tp, 2 * result.tp + result.fp + result.fn);
    if (evaluate_labels) {
        PrintRatio("Classification accuracy (matched balls)", result.correct_labels, result.tp);
        PrintRatio("Correctly identified recall", result.correct_labels, result.tp + result.fn);
    }
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
    const EvaluationResult& result, size_t excluded, size_t images, bool evaluate_labels = true)
{
    WriteCsvField(file, name);
    file << ',' << result.tp << ',' << result.fp << ',' << result.fn
         << ',' << result.tp + result.fn << ',' << result.tp + result.fp
         << ',';
    if (evaluate_labels) file << result.correct_labels;
    else file << "N/A";
    const auto ratio = [&file](double numerator, size_t denominator) {
        file << ',';
        if (denominator == 0) file << "N/A";
        else file << numerator / denominator;
    };
    ratio(result.tp, result.tp + result.fp);
    ratio(result.tp, result.tp + result.fn);
    ratio(2.0 * result.tp, 2 * result.tp + result.fp + result.fn);
    if (evaluate_labels) {
        ratio(result.correct_labels, result.tp);
        ratio(result.correct_labels, result.tp + result.fn);
    } else file << ",N/A,N/A";
    ratio(result.squared_center_error, result.tp);
    file << ',';
    if (result.tp == 0) file << "N/A";
    else file << sqrt(result.squared_center_error / result.tp);
    file << ',' << excluded << ',' << images << ',' << MATCH_DISTANCE << '\n';
}

void WriteEvaluationCsv(const vector<ImageEvaluation>& images,
    const EvaluationResult& total, const vector<ExcludedGroundTruth>& exclusions,
    const vector<ImageEvaluation>& detector_images, const EvaluationResult& detector_total,
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
        const auto write_results = [&](const char* filename, const vector<ImageEvaluation>& rows,
            const EvaluationResult& pooled, bool evaluate_labels) {
            auto results = open_csv(filename);
            results << "image,tp,fp,fn,ground_truth_count,prediction_count,correct_labels,"
                    << "precision,recall,f1,classification_accuracy,correctly_identified_recall,"
                    << "center_mse_pixels_squared,center_rmse_pixels,excluded_ground_truth,"
                    << "evaluated_images,match_distance_pixels\n";
            for (const auto& image : rows)
                WriteEvaluationRow(results, image.filename, image.result, image.excluded_gt, 1, evaluate_labels);
            WriteEvaluationRow(results, "Pooled total", pooled, exclusions.size(), rows.size(), evaluate_labels);
            results.close();
        };
        write_results("results.csv", images, total, true);
        write_results("detector_results.csv", detector_images, detector_total, false);

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

vector<Point> GroundTruthOutline(const BallPrediction& truth, const Mat& inverse,
    double scale, double camera_radius = BALL_RADIUS)
{
    vector<Point2f> center;
    perspectiveTransform(vector<Point2f>{truth.center}, center, inverse);
    // The spreadsheet provides centers, not radii. These are visual circle
    // markers in the camera view; they do not change matching or evaluation.
    vector<Point> outline;
    for (int k = 0; k < 72; ++k) {
        const double angle = 2.0 * CV_PI * k / 72;
        outline.emplace_back(cvRound((center[0].x + camera_radius * cos(angle)) * scale),
            cvRound((center[0].y + camera_radius * sin(angle)) * scale));
    }
    return outline;
}

void DrawOverlayLabel(Mat& image, const string& label, Point position, Scalar colour)
{
    int baseline = 0;
    constexpr double font_scale = 0.46;
    const Size text_size = getTextSize(label, FONT_HERSHEY_SIMPLEX, font_scale, 1, &baseline);
    position.x = max(2, min(position.x - text_size.width / 2, image.cols - text_size.width - 2));
    position.y = max(text_size.height + 2, min(position.y + text_size.height / 2, image.rows - baseline - 2));
    putText(image, label, position, FONT_HERSHEY_SIMPLEX,
        font_scale, Scalar(0, 0, 0), 2, LINE_AA);
    putText(image, label, position, FONT_HERSHEY_SIMPLEX,
        font_scale, colour, 1, LINE_AA);
}

Mat DrawDetectedCircles(const Mat& image, const vector<ClassifiedCircle>& circles,
    const string& filename, size_t candidate_count, bool perspective_view = false)
{
    Mat background = image;
    if (perspective_view) warpPerspective(image, background, TablePerspectiveTransform(), TRANSFORMED_TABLE_SIZE);
    const double scale = min({1.0, 1100.0 / background.cols, 650.0 / background.rows});
    Mat preview;
    resize(background, preview, Size(cvRound(background.cols * scale), cvRound(background.rows * scale)));
    for (const auto& classified : circles) {
        const Vec3f& ball = classified.detection.image_circle;
        const Point2f c = perspective_view ? classified.detection.table_center : Point2f(ball[0], ball[1]);
        const Point center(cvRound(c.x * scale), cvRound(c.y * scale));
        circle(preview, center, cvRound((perspective_view ? BALL_RADIUS : ball[2]) * scale), Scalar(0, 255, 0), 1, LINE_AA);
    }
    // Render every label after every circle so overlaps cannot cover text.
    for (const auto& classified : circles) {
        const Vec3f& ball = classified.detection.image_circle;
        const Point2f c = perspective_view ? classified.detection.table_center : Point2f(ball[0], ball[1]);
        const Point center(cvRound(c.x * scale), cvRound(c.y * scale));
        DrawOverlayLabel(preview, classified.label, center,
            Scalar(255, 255, 255));
    }
    copyMakeBorder(preview, preview, 60, 0, 0, 0, BORDER_CONSTANT, Scalar(30, 30, 30));
    putText(preview, filename + " - " + to_string(circles.size()) + " retained / "
        + to_string(candidate_count) + " candidates", Point(12, 24),
        FONT_HERSHEY_SIMPLEX, 0.6, Scalar(255, 255, 255), 1, LINE_AA);
    putText(preview, "Green circles and white labels: classifier output. Any key: next image; Esc: stop.",
        Point(12, 49), FONT_HERSHEY_SIMPLEX, 0.48, Scalar(220, 220, 220), 1, LINE_AA);
    return preview;
}

Mat DrawGroundTruthCircles(const Mat& image, const vector<BallPrediction>& truth,
    const string& filename, bool perspective_view = false)
{
    Mat background = image;
    if (perspective_view) warpPerspective(image, background, TablePerspectiveTransform(), TRANSFORMED_TABLE_SIZE);
    const double scale = min({1.0, 1100.0 / background.cols, 650.0 / background.rows});
    Mat preview;
    resize(background, preview, Size(cvRound(background.cols * scale), cvRound(background.rows * scale)));
    const Mat inverse = TablePerspectiveTransform().inv();
    for (const auto& actual : truth) {
        Point center;
        if (perspective_view) {
            center = Point(cvRound(actual.center.x * scale), cvRound(actual.center.y * scale));
            circle(preview, center, cvRound((BALL_RADIUS + 3.0) * scale), Scalar(255, 255, 0), 1, LINE_AA);
        } else {
            const vector<Point> circle_outline = GroundTruthOutline(actual, inverse, scale, BALL_RADIUS + 3.0);
            polylines(preview, vector<vector<Point>>{circle_outline}, true, Scalar(255, 255, 0), 1, LINE_AA);
            vector<Point2f> camera_center;
            perspectiveTransform(vector<Point2f>{actual.center}, camera_center, inverse);
            center = Point(cvRound(camera_center[0].x * scale), cvRound(camera_center[0].y * scale));
        }
    }
    // Render every label last so neighboring outlines cannot cross the text.
    for (const auto& actual : truth) {
        Point center;
        if (perspective_view) center = Point(cvRound(actual.center.x * scale), cvRound(actual.center.y * scale));
        else {
            vector<Point2f> camera_center;
            perspectiveTransform(vector<Point2f>{actual.center}, camera_center, inverse);
            center = Point(cvRound(camera_center[0].x * scale), cvRound(camera_center[0].y * scale));
        }
        DrawOverlayLabel(preview, actual.label, center, Scalar(255, 255, 255));
    }
    copyMakeBorder(preview, preview, 60, 0, 0, 0, BORDER_CONSTANT, Scalar(30, 30, 30));
    putText(preview, filename + " - " + to_string(truth.size()) + " valid supplied centers",
        Point(12, 24), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(255, 255, 255), 1, LINE_AA);
    putText(preview, "Thin cyan circles with ball labels: supplied ground truth.",
        Point(12, 49), FONT_HERSHEY_SIMPLEX, 0.48, Scalar(255, 255, 0), 1, LINE_AA);
    return preview;
}

Mat DrawDetectionOutcomes(const Mat& image, const vector<ClassifiedCircle>& circles,
    const vector<BallPrediction>& truth, const string& filename, bool perspective_view = false)
{
    vector<BallPrediction> predictions;
    for (const auto& circle : circles)
        predictions.push_back({circle.detection.table_center, circle.label});
    const vector<int> matches = MatchCenters(predictions, truth);
    const EvaluationResult result = Evaluation(predictions, truth);
    vector<int> prediction_truth(circles.size(), -1);
    for (size_t i = 0; i < matches.size(); ++i)
        if (matches[i] >= 0) prediction_truth[matches[i]] = static_cast<int>(i);
    Mat background = image;
    if (perspective_view) warpPerspective(image, background, TablePerspectiveTransform(), TRANSFORMED_TABLE_SIZE);
    const double scale = min({1.0, 1100.0 / background.cols, 650.0 / background.rows});
    Mat chart;
    resize(background, chart, Size(cvRound(background.cols * scale), cvRound(background.rows * scale)));
    const Mat inverse = TablePerspectiveTransform().inv();
    for (size_t i = 0; i < truth.size(); ++i) {
        if (matches[i] >= 0) continue;
        if (perspective_view) {
            circle(chart, Point(cvRound(truth[i].center.x * scale), cvRound(truth[i].center.y * scale)),
                cvRound(BALL_RADIUS * scale), Scalar(255, 0, 255), 1, LINE_AA);
        } else polylines(chart, vector<vector<Point>>{GroundTruthOutline(truth[i], inverse, scale)},
            true, Scalar(255, 0, 255), 1, LINE_AA);
    }
    for (size_t i = 0; i < circles.size(); ++i) {
        const Vec3f& ball = circles[i].detection.image_circle;
        const Point2f c = perspective_view ? circles[i].detection.table_center : Point2f(ball[0], ball[1]);
        const Point center(cvRound(c.x * scale), cvRound(c.y * scale));
        const int actual = prediction_truth[i];
        const bool label_ok = actual >= 0 && circles[i].label == truth[actual].label;
        const Scalar colour = actual < 0 ? Scalar(0, 0, 255) : (label_ok ? Scalar(0, 190, 0) : Scalar(0, 165, 255));
        circle(chart, center, cvRound((perspective_view ? BALL_RADIUS : ball[2]) * scale), colour, 1, LINE_AA);
    }
    const int card_count = static_cast<int>(circles.size() + result.fn);
    const int report_width = max(chart.cols, 1434);
    const int columns = 4, gap = 10, margin = 12, card_height = 132;
    const int card_width = (report_width - 2 * margin - (columns - 1) * gap) / columns;
    const int rows = (card_count + columns - 1) / columns;
    const int panel_height = 64 + rows * (card_height + gap) + 32;
    Mat report(chart.rows + panel_height, report_width, CV_8UC3, Scalar(248, 248, 248));
    chart.copyTo(report(Rect(0, 0, chart.cols, chart.rows)));
    putText(report, filename + "   |   TP " + to_string(result.tp) + "   FP " + to_string(result.fp) + "   FN " + to_string(result.fn),
        Point(12, chart.rows + 24), FONT_HERSHEY_SIMPLEX, 0.55, Scalar(35, 35, 35), 1, LINE_AA);
    putText(report, "Each crop and caption form one outcome. Distances are in transformed table pixels.",
        Point(12, chart.rows + 46), FONT_HERSHEY_SIMPLEX, 0.43, Scalar(65, 65, 65), 1, LINE_AA);
    int card_index = 0;
    const auto draw_card = [&](const Rect& crop_region, const string& source,
        const string& outcome, const string& labels, const string& distance, const Scalar& colour) {
        const int x = margin + (card_index % columns) * (card_width + gap);
        const int y = chart.rows + 64 + (card_index / columns) * (card_height + gap);
        ++card_index;
        rectangle(report, Rect(x, y, card_width, card_height), Scalar(255, 255, 255), FILLED);
        rectangle(report, Rect(x, y, card_width, card_height), colour, 1);
        const Rect roi = crop_region & Rect(0, 0, image.cols, image.rows);
        if (!roi.empty()) {
            Mat thumb;
            resize(image(roi), thumb, Size(64, 64));
            thumb.copyTo(report(Rect(x + 8, y + 8, 64, 64)));
        } else {
            putText(report, "No crop", Point(x + 9, y + 42),
                FONT_HERSHEY_SIMPLEX, 0.33, Scalar(65, 65, 65), 1, LINE_AA);
        }
        putText(report, outcome, Point(x + 82, y + 25),
            FONT_HERSHEY_SIMPLEX, 0.46, colour, 1, LINE_AA);
        putText(report, "Source: " + source, Point(x + 82, y + 48),
            FONT_HERSHEY_SIMPLEX, 0.42, Scalar(35, 35, 35), 1, LINE_AA);
        putText(report, labels, Point(x + 8, y + 94),
            FONT_HERSHEY_SIMPLEX, 0.43, Scalar(35, 35, 35), 1, LINE_AA);
        putText(report, distance, Point(x + 8, y + 117),
            FONT_HERSHEY_SIMPLEX, 0.40, Scalar(65, 65, 65), 1, LINE_AA);
    };
    for (size_t i = 0; i < circles.size(); ++i) {
        const Vec3f& ball = circles[i].detection.image_circle;
        const int x = cvRound(ball[0]), y = cvRound(ball[1]), radius = max(2, cvRound(ball[2]));
        const Rect crop_region(x - radius, y - radius, 2 * radius + 1, 2 * radius + 1);
        const int actual = prediction_truth[i];
        if (actual < 0) {
            string distance = "Nearest GT distance: n/a";
            if (!truth.empty()) {
                double nearest = numeric_limits<double>::max();
                for (const auto& candidate : truth)
                    nearest = min(nearest, norm(circles[i].detection.table_center - candidate.center));
                distance = "Nearest GT distance: " + format("%.1f px", nearest);
            }
            draw_card(crop_region, "Detection", "FALSE POSITIVE",
                "Truth: unmatched | Predicted: " + circles[i].label, distance, Scalar(0, 0, 255));
        } else {
            const bool label_ok = circles[i].label == truth[actual].label;
            const double distance = norm(circles[i].detection.table_center - truth[actual].center);
            draw_card(crop_region, "Detection", label_ok ? "CORRECT" : "LABEL ERROR",
                "Truth: " + truth[actual].label + " | Predicted: " + circles[i].label,
                "Center distance: " + format("%.1f px", distance),
                label_ok ? Scalar(0, 150, 0) : Scalar(0, 130, 220));
        }
    }
    for (size_t i = 0; i < truth.size(); ++i) {
        if (matches[i] >= 0) continue;
        const Rect crop_region = boundingRect(GroundTruthOutline(truth[i], inverse, 1.0));
        draw_card(crop_region, "Ground truth", "MISSED BALL (FN)",
            "Truth: " + truth[i].label + " | Predicted: none", "Center distance: n/a", Scalar(255, 0, 255));
    }
    const Scalar legend_colours[] = {
        Scalar(0, 190, 0), Scalar(0, 165, 255), Scalar(0, 0, 255), Scalar(255, 0, 255)
    };
    const char* legend_labels[] = {
        "Correct position and label", "Position OK, label error", "FP: unmatched detection", "FN: missed ground-truth ball"
    };
    for (int i = 0; i < 4; ++i) {
        const int x = 12 + i * (report_width / 4);
        circle(report, Point(x + 6, report.rows - 14), 5, legend_colours[i], 2, LINE_AA);
        putText(report, legend_labels[i], Point(x + 18, report.rows - 10),
            FONT_HERSHEY_SIMPLEX, 0.38, Scalar(65, 65, 65), 1, LINE_AA);
    }
    return report;
}

void SaveEvaluationViews(const string& filename, const Mat& ground_truth, const Mat& labels, const Mat& outcomes,
    const Mat& perspective_ground_truth = Mat(), const Mat& perspective_labels = Mat(), const Mat& perspective_outcomes = Mat(),
    const filesystem::path& directory = "output/evaluation/visualizations")
{
    try {
        filesystem::create_directories(directory);
        const string stem = filesystem::path(filename).stem().string();
        if (!imwrite((directory / (stem + "_ground_truth.png")).string(), ground_truth)
            || !imwrite((directory / (stem + "_labels.png")).string(), labels)
            || !imwrite((directory / (stem + "_outcomes.png")).string(), outcomes))
            cerr << "Could not save visualization for " << filename << '\n';
        if (!perspective_ground_truth.empty()) {
            const filesystem::path perspective_directory = directory / "perspective";
            filesystem::create_directories(perspective_directory);
            if (!imwrite((perspective_directory / (stem + "_ground_truth_perspective.png")).string(), perspective_ground_truth)
                || !imwrite((perspective_directory / (stem + "_labels_perspective.png")).string(), perspective_labels)
                || !imwrite((perspective_directory / (stem + "_outcomes_perspective.png")).string(), perspective_outcomes))
                cerr << "Could not save perspective views for " << filename << '\n';
            else
                cout << "Saved " << filename << " perspective views to "
                     << filesystem::absolute(perspective_directory).string() << '\n';
        }
    } catch (const std::exception& error) {
        cerr << "Could not save evaluation views: " << error.what() << '\n';
    }
}

bool LoadManualGroundTruth(const string& filename, array<Point2f, BALL_CLASSES>& centers);

void RunEvaluationPass(bool visualize, bool save_perspective, bool use_corrected,
    const filesystem::path& directory)
{
    // MyApplication loads the source images first; clear those windows before
    // starting the per-table evaluation sequence.
    if (visualize)
        destroyAllWindows();
    cout << "\n=== " << (use_corrected ? "Corrected ground truth (supplied for unedited tables)" : "Supplied ground truth") << " ===\n";
    cout << "\nHough detection + HLS minimum-cost classification: Table1-6; AllBalls excluded as training data.\n"
         << "Transformed image: 1434x683. Match distance <= " << MATCH_DISTANCE
         << " pixels (one ball radius).\n"
         << "Ground truth is provisional; invalid coordinates are excluded, not corrected.\n";
    const Mat all_balls = imread("Media/Pool/AllBalls.jpeg");
    vector<HistogramReference> references;
    try {
        references = BuildReferenceHistograms(all_balls, labeled_circles);
    } catch (const std::exception& error) {
        cerr << "Cannot build reference histograms: " << error.what() << '\n';
        return;
    }
    cout << "Precomputed " << references.size() << " circular HLS references (balls/net/table).\n";
    EvaluationResult total, detector_total;
    size_t excluded = 0, evaluated_images = 0;
    vector<ImageEvaluation> image_results, detector_results;
    vector<ExcludedGroundTruth> exclusions;
    for (const auto& supplied : SuppliedGroundTruth()) {
        const Mat source = imread(string("Media/Pool/") + supplied.filename);
        if (source.empty()) {
            cerr << "Cannot evaluate missing image: " << supplied.filename << '\n';
            continue;
        }
        auto centers = supplied.centers;
        try {
            if (use_corrected && LoadManualGroundTruth(supplied.filename, centers))
                cout << "Using manual ground truth for " << supplied.filename << '\n';
        } catch (const exception& error) {
            cerr << "Cannot evaluate ground truth: " << error.what() << '\n';
            return;
        }
        const size_t excluded_before = excluded;
        vector<BallPrediction> truth;
        for (int i = 0; i < BALL_CLASSES; ++i) {
            const Point2f center = centers[i];
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
        vector<BallPrediction> detector_predictions;
        detector_predictions.reserve(detections.size());
        for (const auto& detection : detections)
            detector_predictions.push_back({detection.table_center, ""});
        const EvaluationResult detector_result = Evaluation(detector_predictions, truth, false);
        PrintEvaluation(string(supplied.filename) + " - detector only", detector_result, false);
        detector_results.push_back({supplied.filename, detector_result, excluded - excluded_before});
        AddEvaluation(detector_total, detector_result);
        const vector<ClassifiedCircle> circles = Classifier(source, detections, references);
        cout << supplied.filename << ": " << detections.size() << " candidates -> "
             << circles.size() << " retained after Classifier\n";
        vector<BallPrediction> predictions;
        for (const auto& circle : circles)
            predictions.push_back({circle.detection.table_center, circle.label});
        const EvaluationResult result = Evaluation(predictions, truth);
        PrintEvaluation(string(supplied.filename) + " - after classification/filtering", result);
        image_results.push_back({supplied.filename, result, excluded - excluded_before});
        AddEvaluation(total, result);
        ++evaluated_images;
        const Mat ground_truth_view = DrawGroundTruthCircles(source, truth, supplied.filename);
        const Mat labels_view = DrawDetectedCircles(source, circles, supplied.filename, detections.size());
        const Mat outcomes_view = DrawDetectionOutcomes(source, circles, truth, supplied.filename);
        Mat perspective_ground_truth, perspective_labels, perspective_outcomes;
        if (save_perspective) {
            perspective_ground_truth = DrawGroundTruthCircles(source, truth, supplied.filename, true);
            perspective_labels = DrawDetectedCircles(source, circles, supplied.filename, detections.size(), true);
            perspective_outcomes = DrawDetectionOutcomes(source, circles, truth, supplied.filename, true);
        }
        SaveEvaluationViews(supplied.filename, ground_truth_view, labels_view, outcomes_view,
            perspective_ground_truth, perspective_labels, perspective_outcomes, directory / "visualizations");
        if (visualize) {
            const Mat& shown_labels = save_perspective ? perspective_labels : labels_view;
            const Mat& shown_ground_truth = save_perspective ? perspective_ground_truth : ground_truth_view;
            const Mat& shown_outcomes = save_perspective ? perspective_outcomes : outcomes_view;
            imshow("Pool ball evaluation", shown_labels);
            if (waitKey(0) == 27) break;
            imshow("Pool ball evaluation", shown_ground_truth);
            if (waitKey(0) == 27) break;
            imshow("Pool ball evaluation", shown_outcomes);
            if (waitKey(0) == 27) break;
        }
    }
    if (visualize && evaluated_images > 0) {
        destroyWindow("Pool ball evaluation");
    }
    PrintEvaluation("Pooled total - detector only", detector_total, false);
    PrintEvaluation("Pooled total - after classification/filtering", total);
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
    WriteEvaluationCsv(image_results, total, exclusions, detector_results, detector_total, directory);
}

void RunEvaluation(bool visualize = true, bool save_perspective = true)
{
    // Always preserve the baseline supplied-truth results. Manual CSVs never replace it.
    RunEvaluationPass(visualize, save_perspective, false, "output/evaluation");
    bool corrected = false;
    for (const auto& supplied : SuppliedGroundTruth()) {
        auto centers = supplied.centers;
        try {
            if (LoadManualGroundTruth(supplied.filename, centers) && centers != supplied.centers)
                corrected = true;
        } catch (const exception& error) {
            cerr << "Cannot load corrected ground truth: " << error.what() << '\n';
            return;
        }
    }
    if (corrected)
        RunEvaluationPass(visualize, save_perspective, true, "output/evaluation/corrected");
    else
        cout << "No saved ground-truth changes; supplied evaluation is sufficient.\n";
}

const char* LABEL_WINDOW = "AllBalls circle detection";

struct LabelSession {
    vector<Vec3f> circles;
    int selected = -1;
    vector<string> labels;
    double display_scale = 1.0;
    int image_height = 0;
    vector<bool> applied;
    vector<Vec3f> table_regions;
    int selected_table = -1;
    bool selecting_table = false;
    bool selecting_net = false;
    bool dragging_table = false;
    bool table_regions_applied = true;
    Point2f drag_center, drag_edge;
    int image_width = 0;
    string message;
};

bool ValidReferenceRegion(const Vec3f& region)
{
    // Manual regions can cross the cloth boundary or overlap other samples.
    // The drag must start inside the image; drawing clips at image edges.
    return isfinite(region[0]) && isfinite(region[1])
        && isfinite(region[2]) && region[2] >= 5;
}

void SelectCircle(int event, int x, int y, int, void* userdata)
{
    auto& session = *static_cast<LabelSession*>(userdata);
    const Point2f point(static_cast<float>(x / session.display_scale),
        static_cast<float>(y / session.display_scale));
    const bool inside_image = x >= 0 && x < session.image_width
        && y >= 0 && y < session.image_height;
    if (session.selecting_table || session.selecting_net) {
        if (event == EVENT_LBUTTONDOWN && inside_image) {
            session.dragging_table = true;
            session.drag_center = session.drag_edge = point;
        } else if (event == EVENT_MOUSEMOVE && session.dragging_table) {
            session.drag_edge = point;
        } else if (event == EVENT_LBUTTONUP && session.dragging_table) {
            session.dragging_table = false;
            const Vec3f region(session.drag_center.x, session.drag_center.y,
                static_cast<float>(norm(point - session.drag_center)));
            if (session.selecting_net && ValidReferenceRegion(region)) {
                session.circles.push_back(region);
                session.labels.push_back("net");
                session.applied.push_back(false);
                session.selected = static_cast<int>(session.circles.size()) - 1;
                session.selected_table = -1;
                session.selecting_net = false;
                session.message = "Net circle added. R adds another; Enter saves all samples.";
            } else if (session.selecting_table && ValidReferenceRegion(region)) {
                session.table_regions.push_back(region);
                session.selected_table = static_cast<int>(session.table_regions.size()) - 1;
                session.selected = -1;
                session.table_regions_applied = false;
                session.selecting_table = false;
                session.message = "Cloth region added. T adds another; Enter saves all samples.";
            } else {
                session.message = session.selecting_net
                    ? "Drag a net circle with its center in the image and radius >= 5. R cancels."
                    : "Drag a reference circle with radius >= 5 pixels. T cancels.";
            }
        }
        return;
    }
    if (event != EVENT_LBUTTONDOWN || !inside_image)
        return;
    int selected = -1;
    double nearest = numeric_limits<double>::max();
    for (size_t i = 0; i < session.circles.size(); ++i) {
        const Vec3f& ball = session.circles[i];
        const double distance = norm(point - Point2f(ball[0], ball[1]));
        if (distance <= ball[2] && distance < nearest) {
            selected = static_cast<int>(i);
            nearest = distance;
        }
    }
    int selected_table = -1;
    for (size_t i = 0; i < session.table_regions.size(); ++i) {
        const Vec3f& region = session.table_regions[i];
        const double distance = norm(point - Point2f(region[0], region[1]));
        if (distance <= region[2] && distance < nearest) {
            selected_table = static_cast<int>(i);
            selected = -1;
            nearest = distance;
        }
    }
    session.selected = selected;
    session.selected_table = selected_table;
    if (selected_table >= 0)
        session.message = "Selected cloth reference. Backspace removes it; T draws another.";
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

string UpdateLabeledCircles(LabelSession& session,
    const filesystem::path& output = "Media/Pool/AllBalls_labels.txt")
{
    vector<LabeledCircle> updated;
    for (size_t i = 0; i < session.circles.size(); ++i) {
        if (session.labels[i].empty()) continue;
        const Vec3f& ball = session.circles[i];
        updated.push_back({session.labels[i], Point2f(ball[0], ball[1]), ball[2]});
    }
    for (const Vec3f& region : session.table_regions)
        updated.push_back({"table", Point2f(region[0], region[1]), region[2]});
    try {
        if (!output.parent_path().empty())
            filesystem::create_directories(output.parent_path());
        filesystem::path temporary = output;
        temporary += ".tmp";
        ofstream file;
        file.exceptions(ios::failbit | ios::badbit);
        file.open(temporary, ios::trunc);
        file.imbue(locale::classic());
        file << "// Source: Media/Pool/AllBalls.jpeg; original image pixel coordinates.\n"
             << "// Circular regions: ball labels, net, and table cloth.\n"
             << "struct LabeledCircle {\n"
             << "    std::string label;\n    cv::Point2f center;\n    float radius;\n};\n\n"
             << "std::vector<LabeledCircle> labeled_circles = {\n";
        file << setprecision(numeric_limits<float>::max_digits10) << showpoint;
        for (const auto& sample : updated)
            file << "    {\"" << sample.label << "\", cv::Point2f("
                 << sample.center.x << "f, " << sample.center.y << "f), "
                 << sample.radius << "f},\n";
        file << "};\n";
        file.close();
        filesystem::rename(temporary, output);
    } catch (const std::exception& error) {
        cerr << "Could not save reference samples: " << error.what() << endl;
        return "Save failed. See terminal; Enter retries.";
    }
    labeled_circles.swap(updated);
    for (size_t i = 0; i < session.labels.size(); ++i)
        session.applied[i] = !session.labels[i].empty();
    session.table_regions_applied = true;
    return "Saved " + to_string(labeled_circles.size()) + " samples ("
        + to_string(session.table_regions.size()) + " cloth) to " + output.string();
}

LabelSession CreateLabelSession(const vector<Vec3f>& circles)
{
    LabelSession session{circles, -1, vector<string>(circles.size())};
    // Preserve existing manual samples when adding just a cloth reference.
    for (const auto& sample : labeled_circles) {
        const Vec3f region(sample.center.x, sample.center.y, sample.radius);
        if (sample.label == "table") {
            session.table_regions.push_back(region);
            continue;
        }
        size_t nearest = session.circles.size();
        double distance = max(5.0f, sample.radius / 2);
        for (size_t i = 0; i < session.circles.size(); ++i) {
            if (!session.labels[i].empty()) continue;
            const auto& circle = session.circles[i];
            const double candidate = norm(sample.center - Point2f(circle[0], circle[1]));
            if (candidate < distance) {
                distance = candidate;
                nearest = i;
            }
        }
        if (nearest == session.circles.size()) {
            session.circles.push_back(region);
            session.labels.push_back(sample.label);
        } else {
            session.circles[nearest] = region;
            session.labels[nearest] = sample.label;
        }
    }
    session.applied.resize(session.circles.size(), false);
    for (size_t i = 0; i < session.labels.size(); ++i)
        session.applied[i] = !session.labels[i].empty();
    return session;
}

void LabelCircles(const Mat& image, const vector<Vec3f>& circles)
{
    LabelSession session = CreateLabelSession(circles);
    session.display_scale = min({1.0, 1100.0 / image.cols, 650.0 / image.rows});
    const int display_width = cvRound(image.cols * session.display_scale);
    session.image_width = display_width;
    session.image_height = cvRound(image.rows * session.display_scale);
    namedWindow(LABEL_WINDOW, WINDOW_AUTOSIZE);
    setMouseCallback(LABEL_WINDOW, SelectCircle, &session);
    string status = "T: draw cloth. R: draw a net circle manually. Enter saves; Esc saves and exits.";
    cout << status << endl;

    while (true) {
        Mat canvas(session.image_height + 125, max(display_width, 850),
            CV_8UC3, Scalar(240, 240, 240));
        Mat image_area = canvas(Rect(0, 0, display_width, session.image_height));
        resize(image, image_area, image_area.size());
        for (size_t i = 0; i < session.circles.size(); ++i) {
            const Vec3f& ball = session.circles[i];
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
        for (size_t i = 0; i < session.table_regions.size(); ++i) {
            const Vec3f& region = session.table_regions[i];
            const Point center(cvRound(region[0] * session.display_scale),
                cvRound(region[1] * session.display_scale));
            const Scalar colour = static_cast<int>(i) == session.selected_table
                ? Scalar(0, 255, 255) : session.table_regions_applied
                ? Scalar(255, 255, 0) : Scalar(0, 165, 255);
            circle(image_area, center, cvRound(region[2] * session.display_scale), colour, 2);
            putText(image_area, "table", center, FONT_HERSHEY_SIMPLEX,
                0.55, Scalar(0, 0, 0), 3, LINE_AA);
            putText(image_area, "table", center, FONT_HERSHEY_SIMPLEX,
                0.55, Scalar(255, 255, 255), 1, LINE_AA);
        }
        if (session.dragging_table) {
            const Point center(cvRound(session.drag_center.x * session.display_scale),
                cvRound(session.drag_center.y * session.display_scale));
            circle(image_area, center,
                cvRound(norm(session.drag_edge - session.drag_center) * session.display_scale),
                Scalar(0, 255, 255), 2);
        }
        if (!session.message.empty()) {
            status = session.message;
            session.message.clear();
        }
        putText(canvas, status, Point(12, session.image_height + 23),
            FONT_HERSHEY_SIMPLEX, 0.48, Scalar(30, 30, 30), 1, LINE_AA);
        putText(canvas, "w: white    n: net    1-9: balls 1-9    a-f: balls 10-15",
            Point(12, session.image_height + 53), FONT_HERSHEY_SIMPLEX,
            0.55, Scalar(30, 30, 30), 1, LINE_AA);
        putText(canvas, "t: draw cloth    r: draw net (center to edge)    Backspace: remove cloth",
            Point(12, session.image_height + 80), FONT_HERSHEY_SIMPLEX,
            0.5, Scalar(60, 60, 60), 1, LINE_AA);
        putText(canvas, "Enter: save all samples to file. Esc: save and exit.",
            Point(12, session.image_height + 108), FONT_HERSHEY_SIMPLEX,
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
        if (key == 't' || key == 'T') {
            session.selecting_table = !session.selecting_table;
            session.selecting_net = false;
            session.dragging_table = false;
            status = session.selecting_table
                ? "Drag from the center of clean cloth to its edge. T cancels."
                : "Circle labeling mode. T selects cloth.";
            continue;
        }
        if (key == 'r' || key == 'R') {
            session.selecting_net = !session.selecting_net;
            session.selecting_table = false;
            session.dragging_table = false;
            status = session.selecting_net
                ? "Drag from the net circle's center to its edge. R cancels."
                : "Circle labeling mode. R draws a net reference.";
            continue;
        }
        if (key == 8 || key == 127) {
            if (!session.table_regions.empty()) {
                const size_t index = session.selected_table >= 0
                    ? static_cast<size_t>(session.selected_table) : session.table_regions.size() - 1;
                session.table_regions.erase(session.table_regions.begin() + index);
                session.selected_table = -1;
                session.table_regions_applied = false;
                status = "Cloth region removed. Enter saves changes.";
            }
            continue;
        }
        if (session.selecting_table || session.selecting_net) continue;
        const string key_label = LabelForKey(key);
        if (session.selected < 0 || key_label.empty())
            continue;
        session.labels[session.selected] = key_label;
        session.applied[session.selected] = false;
        status = "Preview: " + key_label + ". Enter saves all samples.";
    }
    if (getWindowProperty(LABEL_WINDOW, WND_PROP_VISIBLE) >= 1)
        setMouseCallback(LABEL_WINDOW, nullptr, nullptr);
}
// CSV coordinates use the same transformed table space as evaluation.
filesystem::path GroundTruthPath(const string& filename)
{
    return filesystem::path("Media/Pool/ground_truth") /
        (filesystem::path(filename).stem().string() + ".csv");
}

bool LoadManualGroundTruth(const string& filename, array<Point2f, BALL_CLASSES>& centers)
{
    const auto path = GroundTruthPath(filename);
    if (!filesystem::exists(path)) return false;
    ifstream file(path);
    string line;
    if (!file || !getline(file, line) || line != "label,x,y")
        throw runtime_error("Invalid ground-truth header: " + path.string());
    array<Point2f, BALL_CLASSES> loaded;
    loaded.fill(Point2f(-1, -1));
    array<bool, BALL_CLASSES> seen{};
    while (getline(file, line)) {
        istringstream row(line);
        string label, x, y, extra;
        if (!getline(row, label, ',') || !getline(row, x, ',') || !getline(row, y, ',')
            || getline(row, extra, ','))
            throw runtime_error("Invalid ground-truth row: " + path.string());
        const int index = BallClassIndex(label);
        size_t nx = 0, ny = 0;
        const Point2f center(stof(x, &nx), stof(y, &ny));
        if (seen[index] || nx != x.size() || ny != y.size()
            || !isfinite(center.x) || !isfinite(center.y)
            || center.x < 0 || center.y < 0 || center.x >= TRANSFORMED_TABLE_SIZE.width
            || center.y >= TRANSFORMED_TABLE_SIZE.height)
            throw runtime_error("Invalid or duplicate ground-truth center: " + path.string());
        seen[index] = true;
        loaded[index] = center;
    }
    if (file.bad()) throw runtime_error("Cannot read " + path.string());
    centers = loaded;
    return true;
}

void SaveManualGroundTruth(const string& filename, const array<Point2f, BALL_CLASSES>& centers)
{
    string invalid;
    for (int i = 0; i < BALL_CLASSES; ++i) {
        const Point2f point = centers[i];
        if (point == Point2f(-1, -1)) continue;
        if (!isfinite(point.x) || !isfinite(point.y) || point.x < 0 || point.y < 0
            || point.x >= TRANSFORMED_TABLE_SIZE.width || point.y >= TRANSFORMED_TABLE_SIZE.height)
            invalid += (invalid.empty() ? "" : ", ") + BallClassName(i);
    }
    if (!invalid.empty()) throw runtime_error("Correct or delete out-of-bounds balls before saving: " + invalid);
    const auto path = GroundTruthPath(filename);
    filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".tmp";
    ofstream file(temporary);
    file.imbue(locale::classic());
    file << "label,x,y\n" << setprecision(numeric_limits<float>::max_digits10);
    for (int i = 0; i < BALL_CLASSES; ++i)
        if (centers[i] != Point2f(-1, -1))
            file << BallClassName(i) << ',' << centers[i].x << ',' << centers[i].y << '\n';
    file.close();
    if (!file) throw runtime_error("Cannot save " + path.string());
    filesystem::rename(temporary, path);
}

struct GroundTruthSession {
    array<Point2f, BALL_CLASSES> centers;
    Mat transform = TablePerspectiveTransform();
    double scale = 1;
    Size image_size;
    Point2f pending{-1, -1}; // Camera image coordinates.
    int selected = -1;
    bool dragging = false;
    Point2f drag_offset;
    bool dirty = false;
    string status = "Drag a circle to move it; click empty space + label key to add.";
};

void InitializeGroundTruth(GroundTruthSession& session, const GroundTruthImage& supplied)
{
    session.centers = supplied.centers;
    const bool loaded = LoadManualGroundTruth(supplied.filename, session.centers);
    session.dirty = !loaded;
}

void GroundTruthClick(int event, int x, int y, int, void* userdata)
{
    auto& session = *static_cast<GroundTruthSession*>(userdata);
    if (event != EVENT_LBUTTONDOWN && event != EVENT_RBUTTONDOWN
        && event != EVENT_MOUSEMOVE && event != EVENT_LBUTTONUP) return;
    const Point2f camera(static_cast<float>(x / session.scale), static_cast<float>(y / session.scale));
    const bool inside = camera.x >= 0 && camera.y >= 0 && camera.x < session.image_size.width
        && camera.y < session.image_size.height;
    if (session.dragging && (event == EVENT_MOUSEMOVE || event == EVENT_LBUTTONUP)) {
        vector<Point2f> transformed;
        perspectiveTransform(vector<Point2f>{camera + session.drag_offset}, transformed, session.transform);
        const Point2f point = transformed[0];
        if (inside && isfinite(point.x) && isfinite(point.y) && point.x >= 0 && point.y >= 0
            && point.x < TRANSFORMED_TABLE_SIZE.width && point.y < TRANSFORMED_TABLE_SIZE.height
            && point != session.centers[session.selected]) {
            session.centers[session.selected] = point;
            session.dirty = true;
            session.status = "Moved " + BallClassName(session.selected) + ". Enter saves.";
        }
        if (event == EVENT_LBUTTONUP) session.dragging = false;
        return;
    }
    if (!inside || (event != EVENT_LBUTTONDOWN && event != EVENT_RBUTTONDOWN)) return;
    vector<Point2f> position;
    int nearest = -1;
    double distance = BALL_RADIUS;
    Point2f nearest_camera;
    for (int i = 0; i < BALL_CLASSES; ++i) {
        if (session.centers[i] == Point2f(-1, -1)) continue;
        perspectiveTransform(vector<Point2f>{session.centers[i]}, position, session.transform.inv());
        const double candidate = norm(camera - position[0]);
        if (candidate <= distance) {
            nearest = i;
            distance = candidate;
            nearest_camera = position[0];
        }
    }
    session.pending = Point2f(-1, -1);
    session.selected = nearest;
    session.dragging = false;
    if (event == EVENT_RBUTTONDOWN) {
        if (nearest >= 0) {
            session.centers[nearest] = Point2f(-1, -1);
            session.selected = -1;
            session.dirty = true;
            session.status = "Removed " + BallClassName(nearest) + ".";
        }
    } else if (nearest >= 0) {
        session.dragging = true;
        session.drag_offset = nearest_camera - camera;
        session.status = "Selected " + BallClassName(nearest) + ". Drag, press a label key, or Backspace to delete.";
    } else {
        vector<Point2f> transformed;
        perspectiveTransform(vector<Point2f>{camera}, transformed, session.transform);
        const Point2f point = transformed[0];
        if (point.x >= 0 && point.y >= 0 && point.x < TRANSFORMED_TABLE_SIZE.width
            && point.y < TRANSFORMED_TABLE_SIZE.height) {
            session.pending = camera;
            session.status = "Press 1-9, a-f (10-15), or w (White) to add this circle.";
        } else session.status = "Click inside the table boundary.";
    }
}

void ApplyGroundTruthLabel(GroundTruthSession& session, int key)
{
    if (key == 8 || key == 127 || key == 65535 || key == 3014656) {
        session.dragging = false;
        if (session.pending != Point2f(-1, -1)) {
            session.pending = Point2f(-1, -1);
            session.status = "Cancelled pending circle.";
        } else if (session.selected >= 0) {
            session.centers[session.selected] = Point2f(-1, -1);
            session.selected = -1;
            session.dirty = true;
            session.status = "Deleted selected circle.";
        }
        return;
    }
    if (key == '[' || key == ']') {
        session.dragging = false;
        session.pending = Point2f(-1, -1);
        for (int step = 1; step <= BALL_CLASSES; ++step) {
            const int origin = session.selected < 0 ? (key == ']' ? -1 : 0) : session.selected;
            const int candidate = (origin + (key == ']' ? step : -step) + 2 * BALL_CLASSES) % BALL_CLASSES;
            if (session.centers[candidate] != Point2f(-1, -1)) {
                session.selected = candidate;
                session.status = "Selected " + BallClassName(candidate) + ". Backspace deletes; label key changes label.";
                break;
            }
        }
        return;
    }
    const string label = LabelForKey(key);
    if (label.empty() || label == "net") return;
    const int target = BallClassIndex(label);
    if (session.pending != Point2f(-1, -1)) {
        vector<Point2f> transformed;
        perspectiveTransform(vector<Point2f>{session.pending}, transformed, session.transform);
        session.centers[target] = transformed[0];
        session.pending = Point2f(-1, -1);
        session.selected = target;
        session.status = "Placed " + label + ". Enter saves.";
    } else if (session.selected >= 0) {
        // Each label occurs once. Swapping preserves both circles if the target already exists.
        swap(session.centers[target], session.centers[session.selected]);
        session.selected = target;
        session.status = "Label changed to " + label + ". Existing target labels are swapped.";
    } else return;
    session.dragging = false;
    session.dirty = true;
}

void EditGroundTruth()
{
    const char* window = "Ground truth - click ball centers";
    size_t image_index = 0;
    while (image_index < SuppliedGroundTruth().size()) {
        const string filename = SuppliedGroundTruth()[image_index].filename;
        const Mat image = imread("Media/Pool/" + filename);
        if (image.empty()) throw runtime_error("Cannot open " + filename);
        GroundTruthSession session;
        InitializeGroundTruth(session, SuppliedGroundTruth()[image_index]);
        session.scale = min({1.0, 1400.0 / image.cols, 700.0 / image.rows});
        session.image_size = image.size();
        const Size display(cvRound(image.cols * session.scale), cvRound(image.rows * session.scale));
        namedWindow(window, WINDOW_AUTOSIZE);
        setMouseCallback(window, GroundTruthClick, &session);
        bool quit = false;
        int direction = 0;
        while (!quit && direction == 0) {
            Mat canvas(display.height + 175, max(display.width, 1100), CV_8UC3, Scalar(240, 240, 240));
            Mat preview = canvas(Rect(0, 0, display.width, display.height));
            resize(image, preview, display);
            string invalid;
            for (int i = 0; i < BALL_CLASSES; ++i) {
                if (session.centers[i] == Point2f(-1, -1)) continue;
                const Point2f table = session.centers[i];
                if (table.x < 0 || table.y < 0 || table.x >= TRANSFORMED_TABLE_SIZE.width
                    || table.y >= TRANSFORMED_TABLE_SIZE.height)
                    invalid += (invalid.empty() ? "" : ", ") + BallClassName(i);
                vector<Point2f> camera;
                perspectiveTransform(vector<Point2f>{session.centers[i]}, camera, session.transform.inv());
                const Point center(cvRound(camera[0].x * session.scale), cvRound(camera[0].y * session.scale));
                circle(preview, center, max(1, cvRound(BALL_RADIUS * session.scale)),
                    i == session.selected ? Scalar(0, 255, 255) : Scalar(255, 255, 0),
                    i == session.selected ? 2 : 1, LINE_AA);
                DrawOverlayLabel(preview, BallClassName(i), center, Scalar(255, 255, 255));
            }
            if (session.pending != Point2f(-1, -1)) {
                const Point center(cvRound(session.pending.x * session.scale), cvRound(session.pending.y * session.scale));
                circle(preview, center, max(1, cvRound(BALL_RADIUS * session.scale)), Scalar(0, 255, 255), 2, LINE_AA);
                drawMarker(preview, center, Scalar(0, 255, 255), MARKER_CROSS, 7, 1);
            }
            const vector<string> lines = {
                filename + " (" + to_string(image_index + 1) + "/6) | " + session.status,
                "1-9: balls 1-9   a-f: balls 10-15   w: White   Right-click: remove ball",
                "Enter: save   Space: save + next   P: save + previous   Esc: save + exit",
                "Drag: move   Label key: relabel selected (swap if occupied)   Backspace/Delete: remove/cancel",
                "[ / ]: select previous/next circle, including out-of-bounds circles   Radius: 21.5 image pixels",
                invalid.empty() ? "All centers are inside the table." : "Outside table: " + invalid + ". Click + original label to fix, or select + delete."
            };
            for (size_t i = 0; i < lines.size(); ++i)
                putText(canvas, lines[i], Point(12, display.height + 23 + 27 * i),
                    FONT_HERSHEY_SIMPLEX, 0.48, Scalar(30, 30, 30), 1, LINE_AA);
            imshow(window, canvas);
            const int key = waitKey(30);
            const bool closed = getWindowProperty(window, WND_PROP_VISIBLE) < 1;
            ApplyGroundTruthLabel(session, key);
            if (key == 10 || key == 13 || key == ' ' || key == 'p' || key == 'P' || key == 27 || closed) {
                if (session.pending != Point2f(-1, -1) && !closed) {
                    session.status = "Label the pending circle first, or Backspace to cancel it.";
                    continue;
                }
                try {
                    session.dragging = false;
                    if (session.dirty) SaveManualGroundTruth(filename, session.centers);
                    session.dirty = false;
                    session.status = "Saved " + GroundTruthPath(filename).string();
                    if (key == ' ') direction = 1;
                    if ((key == 'p' || key == 'P') && image_index > 0) direction = -1;
                    quit = key == 27 || closed;
                } catch (const exception& error) {
                    if (closed) throw;
                    session.status = error.what();
                }
            }
        }
        if (getWindowProperty(window, WND_PROP_VISIBLE) >= 1) {
            setMouseCallback(window, nullptr, nullptr);
            destroyWindow(window);
        }
        if (quit) return;
        if (direction < 0) --image_index;
        else ++image_index;
    }
}
} // namespace

void LabelGroundTruth()
{
    try {
        EditGroundTruth();
    } catch (const std::exception& error) {
        std::cerr << "Ground-truth editor: " << error.what() << '\n';
        cv::destroyAllWindows();
    }
}


void MyApplication(bool label_mode, bool save_perspective)
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
			RunEvaluation(true, save_perspective);
			waitKey();
		}
	}
	

}

