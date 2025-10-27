#include "api.h"
#include "nlohmann/json.hpp"
#include <chrono>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace std;
using json = nlohmann::json;

// GLOBAL VARIABLES
unsigned char* pixels;
string image_path;

// Algorithm Stats
chrono::time_point<chrono::high_resolution_clock> start_time, end_time;

// STAGE 2 : Structural Analysis : Pattern Matching
struct Pattern {
    int position;
    float module_size;
    int count[5]; // this is sliding window of counts that matched 1:1:3:1:1
};

vector<Pattern> find_patterns(unsigned char* data, int len) {
    /*
      - Takes in a single row or single col of binary pixels
      - Sliding window of 7 pixels. (so the given array should be >= length 7)
      - Checks if the window has 1:1:3:1:1 of b:w:b:w:b
       - valid sliding window is created into a Pattern and added to result
     */
    if (len < 7) return {};
    vector<Pattern> res;

    // state is the number of same color pizels appear
    // example arr=[b b b w w b w b] => {3 2 1 1 1}
    int state[5] = { 0, 0, 0, 0, 0 };
    state[0] = 1;
    int state_idx = 0;
    int previous = data[0];

    auto state_match = [&]() {
        int total = 0;
        for (int i = 0; i < 5; i++) {
            total += state[i];
            if (state[i] == 0) return false;
        }
        if (total < 7) return false;
        float mod_size = total / 7.0f;

        const float TOLERANCE = 0.75f;
        float max_variance = mod_size * TOLERANCE;

        return (abs(state[0] - mod_size * 1) < max_variance * 1 &&
                abs(state[1] - mod_size * 1) < max_variance * 1 &&
                abs(state[2] - mod_size * 3) < max_variance * 3 &&
                abs(state[3] - mod_size * 1) < max_variance * 1 &&
                abs(state[4] - mod_size * 1) < max_variance * 1);
    };

    auto shift_state = [&]() {
        for (int i = 1; i < 5; i++) state[i - 1] = state[i];
        state[4] = 0;
    };

    auto create_pattern_and_add_to_result = [&](int idx) {
        int total = 0;
        for (auto s : state) total += s;
        float mod_size = total / 7.0f;
        int pos = idx - state[4] - state[3] - state[2] / 2;
        Pattern pattern;
        pattern.position = pos;
        pattern.module_size = mod_size;
        for (int i = 0; i < 5; i++) pattern.count[i] = state[i];
        res.push_back(pattern);
    };

    for (int i = 1; i < len; i++) {
        int val = data[i];
        if (val != previous) {
            state_idx++;
            if (state_idx == 5) {
                if (state_match()) create_pattern_and_add_to_result(i);
                shift_state();
                state_idx = 4;
            }
            state[state_idx] = 1;
            previous = val;
        } else {
            state[state_idx]++;
        }
    }

    if (state_idx == 4 && state_match()) create_pattern_and_add_to_result(len);

    return res;
}

// STAGE 3 : Cluster points
struct Cluster {
    double x;
    double y;
    int count;
};

struct Point {
    double x;
    double y;
};

vector<Cluster> get_clusters(vector<Point> points, double tolerance) {
    const double TOLERANCE_SQR = tolerance * tolerance;
    auto get_distance = [](Point p1, Point p2) {
        double dx = p1.x - p2.x;
        double dy = p1.y - p2.y;
        return (dx * dx + dy * dy);
    };
    vector<Cluster> res;
    for (auto& point : points) {
        bool found = false;
        for (auto& clst : res) {
            double dist = get_distance(point, { clst.x, clst.y });
            if (dist < TOLERANCE_SQR) {
                clst.x = (clst.x * clst.count + point.x) / (clst.count + 1);
                clst.y = (clst.y * clst.count + point.y) / (clst.count + 1);
                clst.count++;
                found = true;
                break;
            }
        }
        if (!found) res.push_back({ point.x, point.y, 1 });
    }
    return res;
}

struct Image {
    int width;
    int height;
    int channels;
    unsigned char* pixels;

    // Built after PREPROCESSING, deleted at deconstructor
    unsigned char* grayscale;
    unsigned char* binary_pixels;

    // Constructor
    Image(int width, int height, int channels, unsigned char* pixels) {
        this->width = width;
        this->height = height;
        this->channels = channels;
        this->pixels = pixels;

        do_preprocessing();
    }
    ~Image() {
        if (grayscale) delete[] grayscale;
        if (binary_pixels) delete[] binary_pixels;
    }

public:
    pair<int, int> coords(size_t pixel_idx) {
        size_t pixel_num = pixel_idx / channels;
        size_t start_h = pixel_num / width;
        size_t start_w = pixel_num % width;
        return { start_h, start_w };
    }

    array<int, 3> rgb(size_t pix_idx) {
        return { pixels[pix_idx], pixels[pix_idx + 1], pixels[pix_idx + 2] };
    }

    bool is_transparent(size_t pix_idx) {
        if (this->channels < 4) return false;
        return this->pixels[pix_idx + 3] == 0;
    }

    bool is_black(size_t pix_idx) {
        if (is_transparent(pix_idx)) return false;
        auto [r, g, b] = rgb(pix_idx);
        return (r == 0 && g == 0 && b == 0);
    };

    bool is_white(size_t pix_idx) {
        if (is_transparent(pix_idx)) return false;
        auto [r, g, b] = rgb(pix_idx);
        return (r == 255 && g == 255 && b == 255);
    };

    unsigned char* get_column(int x) {
        unsigned char* col = new unsigned char[height];
        for (int h = 0; h < height; h++) {
            col[h] = binary_pixels[h * width + x];
        }
        return col;
    }

    /*
     * STAGE 1 : PREPROCESSING
     * Build grayscale, using average intensity of rgb
     * Build binary pixels(black:0/white:255) using adaptive thresholding
     * deconstructor deletes grayscale[] and binary_pixels[]
     */
    void do_preprocessing() {
        // Build grayscale
        this->grayscale = new unsigned char[height * width];
        for (int h = 0; h < height; h++) {
            for (int w = 0; w < width; w++) {
                size_t c_idx = (size_t)(h * width + w) * channels;
                size_t g_idx = (size_t)(h * width + w);
                auto [r, g, b] = rgb(c_idx);
                double intensity = (double)(r + g + b) / 3;
                grayscale[g_idx] = (unsigned char)intensity;
            }
        }

        // Build binary pixels
        this->binary_pixels = new unsigned char[height * width];
        const int WINDOW_SIZE = 15;
        const double THRESHOLD_BIAS = 10.0;
        auto get_threshold = [&](int h, int w) {
            double total = 0.0;
            int count = 0; // valid cells
            int half_win = WINDOW_SIZE / 2;
            for (int curr_h = h - half_win; curr_h <= h + half_win; curr_h++) {
                if (curr_h < 0 || curr_h == height) continue;
                for (int curr_w = w - half_win; curr_w <= w + half_win;
                     curr_w++) {
                    if (curr_w < 0 || curr_w == width) continue;
                    size_t g_idx = (size_t)(curr_h * width + curr_w);
                    total += grayscale[g_idx];
                    count++;
                }
            }
            double avg = total / count;
            return (double)(avg - THRESHOLD_BIAS);
        };
        for (int h = 0; h < height; h++) {
            for (int w = 0; w < width; w++) {
                size_t idx = (size_t)(h * width + w);
                double threshold = get_threshold(h, w);
                binary_pixels[idx] = (grayscale[idx] < threshold) ? 0 : 255;
            }
        }
    }

    // Main finder pattern detection
    vector<Cluster> detect_patterns() {
        vector<Point> candidate_points;

        // step 1 : scan all rows horizontally
        for (int r = 0; r < height; r++) {
            unsigned char* row = &binary_pixels[r * width];

            // find horizontal patterns in the row
            vector<Pattern> h_patterns = find_patterns(row, width);

            // step 2: for each horizontal pattern, verify vertically
            for (auto& h_pattern : h_patterns) {
                int center_x = h_pattern.position;
                float mod_size = h_pattern.module_size;

                // Extract the column at this x position
                unsigned char* column = get_column(center_x);

                auto v_patterns = find_patterns(column, height);

                // Use larger tolerance for large images
                float tolerance = mod_size * 1.5f; // ??????????

                // Check if any vertical pattern is neare our current y
                for (auto& v_pattern : v_patterns) {
                    int center_y = v_pattern.position;
                    if (abs(center_y - r) < tolerance) {
                        // verified, add this point
                        candidate_points.push_back(
                            { (double)center_x, (double)center_y });
                        break;
                    }
                }
                delete[] column;
            }
        }

        printf("Total candidate points: %zu\n", candidate_points.size());
        // Step 3: cluster all candidate points
        double cluster_tolerance = max(width, height) * 0.05; // 5% of img size
        auto clusters = get_clusters(candidate_points, cluster_tolerance);

        // Step 4: sort by count (confidence) and return top 3
        sort(clusters.begin(), clusters.end(),
             [](const Cluster& a, const Cluster& b) {
                 return a.count > b.count;
             });

        vector<Cluster> res;
        int num_patterns = min(3, (int)clusters.size());
        for (int i = 0; i < num_patterns; i++) res.push_back(clusters[i]);
        return res;
    }
};

struct QROrientation {
    struct {
        double x, y;
    } top_left, top_right, bottom_left;
    float module_size;
    int version;
    int dimension;
};

// Identify which finder pattern is in which corner using distance
QROrientation determine_orientation(vector<Cluster>& clusters) {
    if (clusters.size() < 3) {
        fprintf(stderr, "Error: Expected 3 , got %zu\n", clusters.size());
        exit(1);
    }

    sort(clusters.begin(), clusters.end(),
         [](const Cluster& a, const Cluster& b) {
             if (abs(a.y - b.y) < 5) return a.x < b.x; // same row, sort by x
             return a.y < b.y;
         });

    QROrientation res;
    // Find top-left (smallest y, and among those, smallest x)
    if (abs(clusters[0].y - clusters[1].y) < 5) {
        // First two have similar y (top row)
        res.top_left = { clusters[0].x, clusters[0].y };
        res.top_right = { clusters[1].x, clusters[1].y };
        res.bottom_left = { clusters[2].x, clusters[2].y };
    } else {
        // Need to figure it out differently
        res.top_left = { clusters[0].x, clusters[0].y };
        res.bottom_left = { clusters[1].x, clusters[1].y };
        res.top_right = { clusters[2].x, clusters[2].y };
    }

    double horizontal_dist = res.top_right.x - res.top_left.x;
    double vertical_dist = res.bottom_left.y - res.top_left.y;

    // QR codes have finder patterns separated by (dimension - 14) modules
    // For version 1: 21 modules total, patterns are 7 modules apart → 21-14=7
    // Estimate: patterns are about (dim-14) modules apart
    float avg_dist = (horizontal_dist + vertical_dist) / 2.0;

    // Guess version based on distance
    // For version 1 (21x21): patterns ~7 modules apart
    // For version 2 (25x25): patterns ~11 modules apart
    int estimated_version = 1;
    int dimension = 21;

    // Try different versions to find best fit
    for (int v = 1; v <= 10; v++) {
        int dim = 17 + 4 * v;
        int pattern_separation = dim - 14; // Patterns are 14 modules from edges
        float expected_module_size = avg_dist / pattern_separation;

        // Check if this makes sense (module size between 1 and 20 pixels)
        if (expected_module_size >= 1.0 && expected_module_size <= 20.0) {
            estimated_version = v;
            dimension = dim;
            break;
        }
    }
    float module_size = avg_dist / (dimension - 14);

    printf("Estimated: version=%d, dimension=%d, module_size=%.2f\n",
           estimated_version, dimension, module_size);

    QROrientation result;
    result.module_size = module_size;
    result.version = estimated_version;
    result.dimension = dimension;

    return res;
}

// [SKIPPED] Stage 5: Resolve perspective of the image
// Stage 6: Grid samplin
vector<vector<bool>> extract_modules(QROrientation& qro, Image& img);

struct FormatInfo {
    int error_correction_lvl;
    int mask_pattern;
};

FormatInfo read_format_info(vector<vector<bool>>& modules, int mask_pattern);

// Apply BCH error correction to format bits
int correct_format_bits(int raw_bits);
// Apply mask pattern to modules
void unmask_modules(vector<vector<bool>>& modules, int mask_pattern);
// Mask formulas for patterns 0-7
int get_mask(int row, int col, int pattern);

// Read bits in the specific serpentine pattern QR uses
vector<uint8_t> read_data_codewords(vector<vector<bool>>& modules, int version,
                                    int error_correction_level);

// Check if position is a function pattern (finder, timing, etc.)
bool is_function_pattern(int row, int col, int version);

// Decode Reed-Solomon error correction
bool reed_solomon_decode(vector<uint8_t>& codewords, int num_data_codewords,
                         int num_ec_codewords);

// Galois Field arithmetic helpers
uint8_t gf_mult(uint8_t a, uint8_t b);
uint8_t gf_div(uint8_t a, uint8_t b);

enum EncodingMode { NUMERIC = 1, ALPHANUMERIC = 2, BYTE = 4, KANJI = 8 };

struct DecodedData {
    string content;
    EncodingMode mode;
};

// Main decoding function
DecodedData decode_data(vector<uint8_t>& codewords, int version);

// Mode-specific decoders
string decode_numeric(const uint8_t* bits, int length);
string decode_alphanumeric(const uint8_t* bits, int length);
string decode_byte(const uint8_t* bits, int length);

// Main pipeline function
string decode_qr_code(Image& img) {
    // 1. Detect finder patterns (already done)
    vector<Cluster> patterns = img.detect_patterns();

    // 2. Determine orientation
    QROrientation orient = determine_orientation(patterns);
    return "";

    /* //
        // 3. Get perspective transform
        Matrix3x3 transform =
            get_perspective_transform(orient.top_left, orient.top_right,
                                      orient.bottom_left, orient.dimension);

        // 4. Extract module grid
        auto modules = extract_modules(img, orient, transform);

        // 5. Read format info
        FormatInfo format = read_format_info(modules, orient.dimension);

        // 6. Unmask
        unmask_modules(modules, format.mask_pattern);

        // 7. Read codewords
        auto codewords = read_data_codewords(modules, orient.version,
                                             format.error_correction_level);

        // 8. Error correction
        reed_solomon_decode(codewords, data_count, ec_count);

        // 9. Decode final data
        DecodedData result = decode_data(codewords, orient.version);

        return result.content;
    */
}

Image build_image() {
    // START GLOBAL TIME HERE
    start_time = chrono::high_resolution_clock::now();

    // image_path = "C:/Users/sreddy/Desktop/qr1.png";
    // image_path = "/mnt/c/Users/sreddy/Desktop/qr1.png";
    image_path = "/Users/smpl/Desktop/qr2.png"; // blank

    int width, height, channels;
    pixels = stbi_load(image_path.c_str(), &width, &height, &channels, 0);

    if (pixels == nullptr) {
        fprintf(stderr, "%s:%d: Failed to load image: %s\n", __FILE__, __LINE__,
                image_path.c_str());
        exit(1);
    }
    Image image = Image(width, height, channels, pixels);
    printf("Image loaded: W=%d, H=%d, Channels=%d\n", width, height, channels);

    // Detect finder patterns & print them
    vector<Cluster> clusters = image.detect_patterns();
    printf("clusters.size: %d\n", (int)clusters.size());
    for (auto c : clusters) printf("%f %f %d\n", c.x, c.y, c.count);

    //  DEBUGGING
    /*
    for (size_t i = 0; i < patterns.size(); i++) {
        printf("Pattern %zd: position: (%.1f, %.1f), detected %d times\n",
               i + 1, patterns[i].x, patterns[i].y, patterns[i].count);
    }
    printf("image.grayscale\n");
    for (int h = 0; h < height; h++) {
        for (int w = 0; w < width; w++) {
            printf("%3d ", (int)image.grayscale[(size_t)(h * width + w)]);
        }
        printf("\n");
    }
    printf("image.binary_pixels\n");
    for (int h = 0; h < height; h++) {
        for (int w = 0; w < width; w++) {
            printf("%3d ", (int)image.binary_pixels[(size_t)(h * width + w)]);
        }
        printf("\n");
    }
    // */

    return image;
}

bool read_input_from_api() {
    // Get the image url
    printf("Stage1: Get Data\n");
    string URL;
    URL = "https://hackattic.com/challenges/reading_qr/"
          "problem?access_token=84173d1e3ccdb099";
    optional<string> data = curl_get(URL);
    if (!data) return EXIT_FAILURE;
    auto json_data = json::parse(*data);
    if (!json_data.contains("image_url")) return EXIT_FAILURE;

    // Save the image as png
    string image_url = json_data["image_url"];
    cout << "saving image from image_url: " << image_url << endl;
    save_image(image_url);
    return 0;
}

void send_response_to_api() {
    // int x = 21;
}

int main() {
    printf("hello world!\n");
    // read_input_from_api();
    start_time = chrono::high_resolution_clock::now();

    Image img = build_image();
    decode_qr_code(img);

    end_time = chrono::high_resolution_clock::now();
    auto diff = chrono::duration<double, milli>(end_time - start_time).count();
    printf("TOTAL TIME: %f ms\n", diff);

    // CLEANUP
    if (pixels) stbi_image_free(pixels); // free up the image, closes the fd

    // send_response_to_api();
    return 0;
}
