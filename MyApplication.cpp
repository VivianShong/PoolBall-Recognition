
#include "Utilities.h"
// Visual Studio defaults to C++14. Use its filesystem TS in that mode.
#if defined(_MSC_VER) && (!defined(_MSVC_LANG) || _MSVC_LANG < 201703L)
#ifndef _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#define _SILENCE_EXPERIMENTAL_FILESYSTEM_DEPRECATION_WARNING
#endif
#include <experimental/filesystem>
namespace app_fs = std::experimental::filesystem;
#else
#include <filesystem>
namespace app_fs = std::filesystem;
#endif
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

// Manually labelled reference samples
// Source: Media/Pool/AllBalls.jpeg (1931 x 509)
// Centers and radii are in original image pixels.

struct LabeledCircle {
    std::string label;
    cv::Point2f center;
    float radius;
};

const std::vector<LabeledCircle> labeled_circles = {
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
    {"table", cv::Point2f(963.500000f, 30.5000000f), 21.0000000f},
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

struct HistogramReference;
const vector<HistogramReference>& ReferenceHistograms();
vector<DetectedCircle> FilterOutTableCandidates(const Mat& image,
    const vector<DetectedCircle>& candidates, const vector<HistogramReference>& references);

// Detection owns coordinate conversion and background filtering.
vector<DetectedCircle> Detector(const Mat& image,
    const vector<HistogramReference>& references = ReferenceHistograms(),
    vector<DetectedCircle>* unfiltered = nullptr)
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
    if (unfiltered) *unfiltered = detections;
    return FilterOutTableCandidates(image, detections, references);
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

// Build once and share the same references between detection and classification.
const vector<HistogramReference>& ReferenceHistograms()
{
    static const auto references = BuildReferenceHistograms(
        imread("Media/Pool/AllBalls.jpeg"), labeled_circles);
    return references;
}

// Stage 1 appearance filter, called after the Hough detector.
vector<DetectedCircle> FilterOutTableCandidates(const Mat& image,
    const vector<DetectedCircle>& candidates, const vector<HistogramReference>& references)
{
    if (candidates.empty()) return {};
    bool has_background = false, has_ball = false;
    for (const auto& reference : references) {
        const bool background = reference.label == "table" || reference.label == "net";
        has_background = has_background || background;
        if (!background) {
            BallClassIndex(reference.label);
            has_ball = true;
        }
    }
    if (!has_background || !has_ball)
        throw invalid_argument("Filtering requires ball and table reference histograms.");
    Mat hls;
    cvtColor(image, hls, COLOR_BGR2HLS);
    vector<DetectedCircle> retained;
    for (const auto& candidate : candidates) {
        const Mat histogram = CalculateHLSHistogram(hls, candidate.image_circle);
        if (histogram.empty()) continue;
        double background_distance = 2.0, ball_distance = 2.0;
        for (const auto& reference : references) {
            const double distance = compareHist(histogram, reference.histogram, HISTCMP_BHATTACHARYYA);
            if (reference.label == "table" || reference.label == "net")
                background_distance = min(background_distance, distance);
            else
                ball_distance = min(ball_distance, distance);
        }
        // Equal distances favor rejection, matching the classifier's rule.
        if (ball_distance < background_distance) retained.push_back(candidate);
    }
    return retained;
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
    // Snapshot of "Ball Locations In Transformed Image (Second correction).xlsx", Sheet1, rows 4-9.
    // Workbook Row,Column pairs become Point2f(Column,Row). Blank pairs are absent.
    // AllBalls is training data and is deliberately excluded from test totals.
    // Values are preserved as supplied; out-of-bounds entries are flagged below.
    static const vector<GroundTruthImage> truth = {
        {"Table1.jpeg", {{
            Point2f(1092.0f, 309.0f), Point2f(1212.0f, 429.0f), Point2f(1131.0f, 383.0f), Point2f(1050.0f, 335.0f),
            Point2f(1211.0f, 285.0f), Point2f(1170.0f, 261.0f), Point2f(1173.0f, 352.0f), Point2f(1132.0f, 331.0f),
            Point2f(1210.0f, 240.0f), Point2f(1170.0f, 310.0f), Point2f(1169.0f, 407.0f), Point2f(1135.0f, 281.0f),
            Point2f(1212.0f, 374.0f), Point2f(1091.0f, 360.0f), Point2f(1213.0f, 331.0f), Point2f(29.0f, 288.0f)
        }}},
        {"Table2.jpeg", {{
            Point2f(1103.0f, 282.0f), Point2f(1224.0f, 620.0f), Point2f(1121.0f, 391.0f), Point2f(1046.0f, 315.0f),
            Point2f(1305.0f, 310.0f), Point2f(621.0f, 202.0f), Point2f(1173.0f, 361.0f), Point2f(1132.0f, 331.0f),
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
    const vector<ImageEvaluation>& hough_images, const EvaluationResult& hough_total,
    const app_fs::path& directory = "output/evaluation")
{
    try {
        app_fs::create_directories(directory);
        const auto open_csv = [&directory](const char* filename) {
            ofstream file;
            file.exceptions(ios::failbit | ios::badbit);
            file.open((directory / filename).string(), ios::trunc);
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
        write_results("hough_results.csv", hough_images, hough_total, false);

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
    const app_fs::path& directory = "output/evaluation/visualizations")
{
    try {
        app_fs::create_directories(directory);
        const string stem = app_fs::path(filename).stem().string();
        if (!imwrite((directory / (stem + "_ground_truth.png")).string(), ground_truth)
            || !imwrite((directory / (stem + "_labels.png")).string(), labels)
            || !imwrite((directory / (stem + "_outcomes.png")).string(), outcomes))
            cerr << "Could not save visualization for " << filename << '\n';
        if (!perspective_ground_truth.empty()) {
            const app_fs::path perspective_directory = directory / "perspective";
            app_fs::create_directories(perspective_directory);
            if (!imwrite((perspective_directory / (stem + "_ground_truth_perspective.png")).string(), perspective_ground_truth)
                || !imwrite((perspective_directory / (stem + "_labels_perspective.png")).string(), perspective_labels)
                || !imwrite((perspective_directory / (stem + "_outcomes_perspective.png")).string(), perspective_outcomes))
                cerr << "Could not save perspective views for " << filename << '\n';
            else
                cout << "Saved " << filename << " perspective views to "
                     << app_fs::absolute(perspective_directory).string() << '\n';
        }
    } catch (const std::exception& error) {
        cerr << "Could not save evaluation views: " << error.what() << '\n';
    }
}

// Draw either raw or filtered predictions without repeating detection or calibration.
void SaveCandidateViews(const Mat& source, const vector<DetectedCircle>& predictions,
    const app_fs::path& directory, const string& camera_name, const string& table_name)
{
    Mat camera_view = source.clone(), table_view;
    warpPerspective(source, table_view, TablePerspectiveTransform(), TRANSFORMED_TABLE_SIZE);
    for (const auto& prediction : predictions) {
        const Vec3f c = prediction.image_circle;
        circle(camera_view, Point(cvRound(c[0]), cvRound(c[1])), cvRound(c[2]),
            Scalar(0, 255, 0), 2, LINE_AA);
        circle(table_view, Point(cvRound(prediction.table_center.x), cvRound(prediction.table_center.y)),
            cvRound(BALL_RADIUS), Scalar(0, 255, 0), 1, LINE_AA);
    }
    imwrite((directory / camera_name).string(), camera_view);
    imwrite((directory / table_name).string(), table_view);
}

// Report-only detection views and histogram summaries; no parameter changes.
void SaveReportProcessingViews()
{
    const app_fs::path directory = "output/evaluation/report_assets";
    app_fs::create_directories(directory);
    const Mat training = imread("Media/Pool/AllBalls.jpeg");
    const Mat source = imread("Media/Pool/Table3.jpeg");
    if (training.empty() || source.empty())
        throw runtime_error("Cannot load report illustration images.");
    const auto references = BuildReferenceHistograms(training, labeled_circles);
    vector<DetectedCircle> unfiltered;
    const auto detections = Detector(source, references, &unfiltered);
    Mat grey;
    cvtColor(source, grey, COLOR_BGR2GRAY);
    imwrite((directory / "Table3_input.png").string(), source);
    imwrite((directory / "Table3_grey.png").string(), grey);
    SaveCandidateViews(source, detections, directory, "Table3_candidates.png", "Table3_rectified.png");
    SaveCandidateViews(source, unfiltered, directory, "Table3_hough_candidates.png", "Table3_hough_rectified.png");
    Mat samples = training.clone();
    for (const auto& sample : labeled_circles) {
        const Point center(cvRound(sample.center.x), cvRound(sample.center.y));
        circle(samples, center, cvRound(sample.radius), Scalar(0, 255, 255), 2, LINE_AA);
        DrawOverlayLabel(samples, sample.label, center, Scalar(255, 255, 255));
    }
    imwrite((directory / "AllBalls_samples.png").string(), samples);
    // Marginals illustrate the actual joint descriptor, without replacing it.
    ofstream marginals((directory / "reference_marginals.csv").string());
    marginals << "label,channel,bin,probability\n" << setprecision(10);
    for (const auto& reference : references) {
        for (int channel = 0; channel < 3; ++channel) {
            const int bin_count = channel == 0 ? 18 : 8;
            vector<double> values(bin_count, 0.0);
            for (int h = 0; h < 18; ++h)
                for (int l = 0; l < 8; ++l)
                    for (int s = 0; s < 8; ++s)
                        values[channel == 0 ? h : channel == 1 ? l : s]
                            += reference.histogram.at<float>(h, l, s);
            for (int bin = 0; bin < bin_count; ++bin)
                marginals << reference.label << ',' << channel << ',' << bin << ',' << values[bin] << '\n';
        }
    }
}

// Assemble full-resolution panels; only failure crops are enlarged for inspection.
Mat ReportPanels(const vector<Mat>& images, const vector<string>& titles, int columns)
{
    if (images.empty() || images.size() != titles.size())
        throw invalid_argument("Report panels require an image and title for each panel.");
    int width = 0, height = 0;
    for (const auto& image : images) {
        if (image.empty()) throw runtime_error("Missing report panel image.");
        width = max(width, image.cols);
        height = max(height, image.rows);
    }
    const double scale = max(0.7, width / 1100.0);
    const int header = cvRound(70 * scale), gap = 24;
    const int rows = (static_cast<int>(images.size()) + columns - 1) / columns;
    Mat panel(rows * (height + header + gap) + gap,
        columns * (width + gap) + gap, CV_8UC3, Scalar(255, 255, 255));
    for (size_t i = 0; i < images.size(); ++i) {
        const int x = gap + (static_cast<int>(i) % columns) * (width + gap);
        const int y = gap + (static_cast<int>(i) / columns) * (height + header + gap);
        putText(panel, titles[i], Point(x, y + cvRound(43 * scale)),
            FONT_HERSHEY_SIMPLEX, 1.7 * scale, Scalar(20, 20, 20), 2, LINE_AA);
        Mat color;
        if (images[i].channels() == 1) cvtColor(images[i], color, COLOR_GRAY2BGR);
        else color = images[i];
        color.copyTo(panel(Rect(x + (width - color.cols) / 2,
            y + header + (height - color.rows) / 2, color.cols, color.rows)));
    }
    return panel;
}

void SaveHistogramFigure(const vector<HistogramReference>& references, const app_fs::path& directory)
{
    Mat chart(720, 2400, CV_8UC3, Scalar(255, 255, 255));
    const vector<string> names = {"Hue", "Lightness", "Saturation"};
    for (int channel = 0; channel < 3; ++channel) {
        const int bins = channel == 0 ? 18 : 8;
        const int left = 100 + channel * 800, top = 80, width = 650, height = 450;
        const auto position = [&](int bin, double value) {
            return Point(left + cvRound(bin * width / double(bins - 1)),
                top + height - cvRound(value * height));
        };
        putText(chart, names[channel], Point(left + 160, 40), FONT_HERSHEY_SIMPLEX,
            1.1, Scalar(0, 0, 0), 2, LINE_AA);
        for (int tick = 0; tick <= 4; ++tick) {
            const int y = top + height - tick * height / 4;
            line(chart, Point(left, y), Point(left + width, y), Scalar(220, 220, 220), 1);
            putText(chart, format("%.2f", tick / 4.0), Point(left - 90, y + 9),
                FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 0, 0), 1, LINE_AA);
        }
        rectangle(chart, Rect(left, top, width + 1, height + 1), Scalar(0, 0, 0), 2);
        for (int bin = 0; bin < bins; ++bin) {
            const Point p = position(bin, 0);
            line(chart, p, p + Point(0, 8), Scalar(0, 0, 0), 2);
            if (bins == 8 || bin % 4 == 0 || bin == bins - 1)
                putText(chart, to_string(bin), p + Point(-10, 38), FONT_HERSHEY_SIMPLEX,
                    0.8, Scalar(0, 0, 0), 1, LINE_AA);
        }
        putText(chart, "Bin", Point(left + 290, 610), FONT_HERSHEY_SIMPLEX,
            1.0, Scalar(0, 0, 0), 2, LINE_AA);
        for (const auto& reference : references) {
            const bool background = reference.label == "table" || reference.label == "net";
            if (!background && reference.label != "1" && reference.label != "9" && reference.label != "White") continue;
            vector<double> marginal(bins, 0);
            for (int h = 0; h < 18; ++h)
                for (int l = 0; l < 8; ++l)
                    for (int sat = 0; sat < 8; ++sat)
                        marginal[channel == 0 ? h : channel == 1 ? l : sat] += reference.histogram.at<float>(h, l, sat);
            const Scalar color = background ? Scalar(69, 139, 35) : reference.label == "1"
                ? Scalar(0, 185, 230) : reference.label == "9" ? Scalar(180, 110, 0) : Scalar(110, 110, 110);
            for (int bin = 1; bin < bins; ++bin) {
                const Point a = position(bin - 1, marginal[bin - 1]), b = position(bin, marginal[bin]);
                if (!background) line(chart, a, b, color, 4, LINE_AA);
                else {
                    const double length = norm(b - a);
                    for (double d = 0; d < length; d += 18) {
                        const Point2d delta(b.x - a.x, b.y - a.y);
                        const Point2d begin = Point2d(a.x, a.y) + delta * (d / length);
                        const Point2d end = Point2d(a.x, a.y) + delta * (min(d + 10, length) / length);
                        line(chart, Point(cvRound(begin.x), cvRound(begin.y)),
                            Point(cvRound(end.x), cvRound(end.y)), color, 2, LINE_AA);
                    }
                }
            }
        }
    }
    putText(chart, "Probability", Point(16, 36), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 0, 0), 1, LINE_AA);
    const vector<string> labels = {"1", "9", "White", "table: 6 samples (dashed)"};
    const vector<Scalar> colors = {Scalar(0,185,230), Scalar(180,110,0), Scalar(110,110,110), Scalar(69,139,35)};
    for (int i = 0; i < 4; ++i) {
        const int x = 270 + i * 440;
        line(chart, Point(x, 675), Point(x + 60, 675), colors[i], 4, LINE_AA);
        putText(chart, labels[i], Point(x + 80, 685), FONT_HERSHEY_SIMPLEX,
            0.85, Scalar(0, 0, 0), 2, LINE_AA);
    }
    imwrite((directory / "histogram_marginals.png").string(), chart);
}

void SaveConfusionFigure(const EvaluationResult& total, const app_fs::path& directory)
{
    const int left = 150, top = 80, cell_width = 88, cell_height = 46;
    Mat chart(990, 1740, CV_8UC3, Scalar(255, 255, 255));
    putText(chart, "Truth", Point(15, 50), FONT_HERSHEY_SIMPLEX, 1.4, Scalar(0,0,0), 2, LINE_AA);
    for (int i = 0; i <= BALL_CLASSES; ++i) {
        const string label = i == BACKGROUND ? "BG" : i == 15 ? "W" : to_string(i + 1);
        putText(chart, label, Point(left - 75, top + i * cell_height + 33),
            FONT_HERSHEY_SIMPLEX, 1.4, Scalar(0,0,0), 2, LINE_AA);
        putText(chart, label, Point(left + i * cell_width + 22, top + 17 * cell_height + 50),
            FONT_HERSHEY_SIMPLEX, 1.4, Scalar(0,0,0), 2, LINE_AA);
        for (int j = 0; j <= BALL_CLASSES; ++j) {
            const int count = static_cast<int>(total.confusion[i][j]);
            const double strength = min(1.0, count / 6.0);
            const Scalar fill(255 - 110 * strength, 250 - 210 * strength, 245 - 235 * strength);
            rectangle(chart, Rect(left + j * cell_width, top + i * cell_height, cell_width, cell_height), fill, FILLED);
            if (count) putText(chart, to_string(count), Point(left + j * cell_width + 30, top + i * cell_height + 33),
                FONT_HERSHEY_SIMPLEX, 1.4, i != j ? Scalar(0,110,240) : count >= 4 ? Scalar(255,255,255) : Scalar(0,0,0), 2, LINE_AA);
        }
    }
    // Light cell boundaries make counts easier to trace across rows and columns.
    const Scalar grid_color(215, 220, 225);
    for (int boundary = 1; boundary <= BALL_CLASSES; ++boundary) {
        line(chart, Point(left + boundary * cell_width, top),
            Point(left + boundary * cell_width, top + 17 * cell_height), grid_color, 2, LINE_AA);
        line(chart, Point(left, top + boundary * cell_height),
            Point(left + 17 * cell_width, top + boundary * cell_height), grid_color, 2, LINE_AA);
    }
    rectangle(chart, Rect(left, top, 17 * cell_width, 17 * cell_height), Scalar(0,0,0), 2);
    putText(chart, "Predicted identity", Point(650, 965), FONT_HERSHEY_SIMPLEX, 1.5, Scalar(0,0,0), 2, LINE_AA);
    imwrite((directory / "updated_gt_confusion.png").string(), chart);
}

// Generate every report illustration from reference data and current predictions.
void SaveReportFigures(const vector<HistogramReference>& references, const EvaluationResult& total,
    const app_fs::path& evaluation_directory)
{
    const auto directory = evaluation_directory / "report_assets";
    app_fs::create_directories(directory);
    vector<Mat> stages;
    for (const string name : {"Table3_input.png", "Table3_grey.png", "Table3_hough_candidates.png", "Table3_hough_rectified.png"})
        stages.push_back(imread((directory / name).string()));
    imwrite((directory / "processing_stages.png").string(), ReportPanels(stages,
        {"(a) Camera image", "(b) Grayscale", "(c) All Hough candidates", "(d) All transformed centers"}, 2));
    SaveHistogramFigure(references, directory);
    SaveConfusionFigure(total, directory);
    vector<Mat> truth_views, outcome_views, failures;
    vector<string> truth_titles, outcome_titles, failure_titles;
    for (const auto& supplied : SuppliedGroundTruth()) {
        const Mat source = imread(string("Media/Pool/") + supplied.filename);
        if (source.empty()) throw runtime_error("Missing report source image.");
        vector<BallPrediction> truth, predictions;
        for (int i = 0; i < BALL_CLASSES; ++i) {
            const Point2f c = supplied.centers[i];
            if (isfinite(c.x) && isfinite(c.y) && c.x >= 0 && c.y >= 0
                && c.x < TRANSFORMED_TABLE_SIZE.width && c.y < TRANSFORMED_TABLE_SIZE.height)
                truth.push_back({c, BallClassName(i)});
        }
        const auto detected = Detector(source, references);
        const auto classified = Classifier(source, detected, references);
        for (const auto& ball : classified) predictions.push_back({ball.detection.table_center, ball.label});
        const auto matches = MatchCenters(predictions, truth);
        vector<bool> used(predictions.size(), false);
        Mat table;
        warpPerspective(source, table, TablePerspectiveTransform(), TRANSFORMED_TABLE_SIZE);
        Mat actual = table.clone(), outcome = table.clone();
        const string name = app_fs::path(supplied.filename).stem().string();
        if (outcome_views.empty()) {
            Mat positions = table.clone();
            for (const auto& prediction : predictions)
                circle(positions, Point(cvRound(prediction.center.x), cvRound(prediction.center.y)),
                    cvRound(BALL_RADIUS), Scalar(0,255,0), 2, LINE_AA);
            for (const auto& prediction : predictions)
                DrawOverlayLabel(positions, prediction.label,
                    Point(cvRound(prediction.center.x), cvRound(prediction.center.y)), Scalar(255,255,255));
            imwrite((directory / "Table1_labels_perspective.png").string(), positions);
        }
        const auto failure_crop = [&](const Point2f& center, const Scalar& color, const string& title) {
            const int side = 130;
            const Rect region = Rect(cvRound(center.x) - side/2, cvRound(center.y) - side/2, side, side)
                & Rect(0,0,table.cols,table.rows);
            Mat crop;
            resize(table(region), crop, Size(), 4.0, 4.0, INTER_CUBIC);
            circle(crop, Point(cvRound((center.x - region.x)*4), cvRound((center.y - region.y)*4)),
                cvRound(BALL_RADIUS*4), color, 5, LINE_AA);
            failures.push_back(crop);
            failure_titles.push_back(title);
        };
        for (size_t i = 0; i < truth.size(); ++i) {
            const Point c(cvRound(truth[i].center.x), cvRound(truth[i].center.y));
            circle(actual, c, cvRound(BALL_RADIUS), Scalar(255,255,0), 2, LINE_AA);
            DrawOverlayLabel(actual, truth[i].label, c, Scalar(255,255,0));
            if (matches[i] < 0) {
                circle(outcome, c, cvRound(BALL_RADIUS), Scalar(255,0,255), 3, LINE_AA);
                DrawOverlayLabel(outcome, "miss " + truth[i].label, c, Scalar(255,0,255));
                failure_crop(truth[i].center, Scalar(255,0,255), name + ": " + truth[i].label + " missed");
                continue;
            }
            const int j = matches[i];
            used[j] = true;
            const bool correct = truth[i].label == predictions[j].label;
            const Scalar color = correct ? Scalar(0,190,0) : Scalar(0,165,255);
            const Point p(cvRound(predictions[j].center.x), cvRound(predictions[j].center.y));
            circle(outcome, p, cvRound(BALL_RADIUS), color, 2, LINE_AA);
            DrawOverlayLabel(outcome, correct ? predictions[j].label : truth[i].label + ">" + predictions[j].label, p, color);
            if (!correct) failure_crop(predictions[j].center, color,
                name + ": " + truth[i].label + " -> " + predictions[j].label);
        }
        for (size_t j = 0; j < predictions.size(); ++j) if (!used[j]) {
            const Point p(cvRound(predictions[j].center.x), cvRound(predictions[j].center.y));
            circle(outcome, p, cvRound(BALL_RADIUS), Scalar(0,0,255), 2, LINE_AA);
            DrawOverlayLabel(outcome, "FP " + predictions[j].label, p, Scalar(0,0,255));
            failure_crop(predictions[j].center, Scalar(0,0,255), name + ": FP " + predictions[j].label);
        }
        if (truth_views.size() < 2) {
            truth_views.push_back(actual);
            truth_titles.push_back(name + ": supplied ground truth");
        }
        outcome_views.push_back(outcome);
        outcome_titles.push_back(name);
    }
    imwrite((directory / "updated_supplied_ground_truth.png").string(), ReportPanels(truth_views, truth_titles, 2));
    imwrite((directory / "updated_gt_outcomes_perspective.png").string(), ReportPanels(outcome_views, outcome_titles, 2));
    if (!failures.empty()) imwrite((directory / "updated_gt_failure_crops.png").string(),
        ReportPanels(failures, failure_titles, min(3, static_cast<int>(failures.size()))));
}

void RunEvaluationPass(bool visualize, bool save_perspective,
    const app_fs::path& directory)
{
    // MyApplication loads the source images first; clear those windows before
    // starting the per-table evaluation sequence.
    if (visualize)
        destroyAllWindows();
    cout << "\n=== Supplied ground truth ===\n";
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
    cout << "Precomputed " << references.size() << " circular HLS references (balls/table background).\n";
    const auto hough_directory = directory / "visualizations" / "hough";
    app_fs::create_directories(hough_directory);
    EvaluationResult total, detector_total, hough_total;
    size_t excluded = 0, evaluated_images = 0;
    vector<ImageEvaluation> image_results, detector_results, hough_results;
    vector<ExcludedGroundTruth> exclusions;
    for (const auto& supplied : SuppliedGroundTruth()) {
        const Mat source = imread(string("Media/Pool/") + supplied.filename);
        if (source.empty()) {
            cerr << "Cannot evaluate missing image: " << supplied.filename << '\n';
            continue;
        }
        const auto& centers = supplied.centers;
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
        vector<DetectedCircle> unfiltered;
        const vector<DetectedCircle> detections = Detector(source, references, &unfiltered);

        // Save unfiltered Hough candidates for every table in both coordinate systems.
        const string table_name = app_fs::path(supplied.filename).stem().string();
        SaveCandidateViews(source, unfiltered, hough_directory,
            table_name + "_hough_candidates.png", table_name + "_hough_rectified.png");

        // Evaluate Hough candidates before histogram filtering.
        vector<BallPrediction> hough_predictions;
        hough_predictions.reserve(unfiltered.size());
        for (const auto& detection : unfiltered)
            hough_predictions.push_back({detection.table_center, ""});
        const EvaluationResult hough_result = Evaluation(hough_predictions, truth, false);
        PrintEvaluation(string(supplied.filename) + " - before histogram filtering", hough_result, false);
        hough_results.push_back({supplied.filename, hough_result, excluded - excluded_before});
        AddEvaluation(hough_total, hough_result);

        // Evaluate Stage 1 results after histogram filtering.
        vector<BallPrediction> detector_predictions;
        detector_predictions.reserve(detections.size());
        for (const auto& detection : detections)
            detector_predictions.push_back({detection.table_center, ""});
        const EvaluationResult detector_result = Evaluation(detector_predictions, truth, false);
        PrintEvaluation(string(supplied.filename) + " - detector only", detector_result, false);
        detector_results.push_back({supplied.filename, detector_result, excluded - excluded_before});
        AddEvaluation(detector_total, detector_result);

        // Classify the retained candidates and evaluate Stage 2 results.
        const vector<ClassifiedCircle> circles = Classifier(source, detections, references);
        vector<BallPrediction> predictions;
        for (const auto& circle : circles)
            predictions.push_back({circle.detection.table_center, circle.label});
        const EvaluationResult result = Evaluation(predictions, truth);
        PrintEvaluation(string(supplied.filename) + " - after classification/filtering", result);
        image_results.push_back({supplied.filename, result, excluded - excluded_before});
        AddEvaluation(total, result);
        ++evaluated_images;

        // Generate, save and optionally display ground truth, predictions and evaluation outcomes.
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
    PrintEvaluation("Pooled total - before histogram filtering", hough_total, false);
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
    WriteEvaluationCsv(image_results, total, exclusions, detector_results, detector_total,
        hough_results, hough_total, directory);
    if (evaluated_images == SuppliedGroundTruth().size()) {
        try {
            SaveReportFigures(references, total, directory);
        } catch (const exception& error) {
            cerr << "Could not save report figures: " << error.what() << '\n';
        }
    }
}

void RunEvaluation(bool visualize = true, bool save_perspective = true)
{
    try {
        SaveReportProcessingViews();
    } catch (const exception& error) {
        cerr << "Could not save report processing views: " << error.what() << '\n';
    }
    RunEvaluationPass(visualize, save_perspective, "output/evaluation");
}

} // namespace

void MyApplication()
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
	// Show detected circles in the reference image
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
		RunEvaluation();
		waitKey();
	}
}
