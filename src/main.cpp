#include "api.h"
#include "nlohmann/json.hpp"
#include <chrono>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace std;
using json = nlohmann::json;

// Algorithm Stats
chrono::time_point<chrono::high_resolution_clock> start_time, end_time;

// STAGE 2 : Structural Analysis : Pattern Matching
struct Pattern {
    int position;
    float module_size;
    int count[5];
};

vector<Pattern> find_patterns(unsigned char* data, int len) {
    if (len < 7) return {};
    vector<Pattern> res;

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

    auto add_pattern = [&](int idx) {
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
                if (state_match()) add_pattern(i);
                shift_state();
                state_idx = 4;
            }
            state[state_idx] = 1;
            previous = val;
        } else {
            state[state_idx]++;
        }
    }

    if (state_idx == 4 && state_match()) add_pattern(len);

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
vector<Cluster> get_clusters(vector<Point> points, double tolerance = 10.0) {
    const double TOLERANCE_SQR = tolerance * tolerance;
    vector<Cluster> res;

    auto get_distance = [](Point p1, Point p2) {
        double dx = p1.x - p2.x;
        double dy = p1.y - p2.y;
        return (dx * dx + dy * dy);
    };

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
    size_t color_idx(int h, int w) {
        return (size_t)(h * width + w) * channels;
    }

    size_t gray_idx(int h, int w) {
        return (size_t)(h * width + w);
    }

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
                float mod_size = h_pattern.module_size;

                // Extract the column at this x position
                unsigned char* column = get_column(h_pattern.position);

                auto v_patterns = find_patterns(column, height);
                // if (v_patterns.size()) {
                //     printf("----\n");
                //     printf("y:%d h_pats.size():%ld\n", r, h_patterns.size());
                //     printf("y:%d v_pats.size():%ld\n", r, v_patterns.size());
                // }

                // Use larger tolerance for large images
                float tolerance = mod_size * 10.0f;

                // Check if any vertical pattern is neare our current y
                for (auto& v_pattern : v_patterns) {
                    int center_y = v_pattern.position;
                    // printf("h.pos:%d v.pos:%d\n", h_pattern.position,
                    //        v_pattern.position);
                    // printf("center_y: %d l:%d r:%fd\n", center_y,
                    //        abs(center_y - r), mod_size);
                    // verify: is vertical pattern close to our row?
                    if (abs(center_y - r) < tolerance) {
                        // verified, add this point
                        candidate_points.push_back(
                            { (double)h_pattern.position, (double)center_y });
                        break;
                    }
                }
                delete[] column;
            }
        }

        printf("Total candidate points: %zu\n", candidate_points.size());
        // Step 3: cluster all candidate points
        double cluster_tolerance = max(width, height) * 0.05; // 5% of img size
        vector<Cluster> clusters =
            get_clusters(candidate_points, cluster_tolerance);

        // Step 4: sort by count (confidence) and return top 3
        sort(clusters.begin(), clusters.end(),
             [](const Cluster& a, const Cluster& b) {
                 return a.count > b.count;
             });

        vector<Cluster> finder_patterns;
        int num_patterns = min(3, (int)clusters.size());
        for (int i = 0; i < num_patterns; i++)
            finder_patterns.push_back(clusters[i]);
        return finder_patterns;
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
                size_t c_idx = color_idx(h, w);
                size_t g_idx = gray_idx(h, w);
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
                    size_t g_idx = gray_idx(curr_h, curr_w);
                    total += grayscale[g_idx];
                    count++;
                }
            }
            double avg = total / count;
            return (double)(avg - THRESHOLD_BIAS);
        };
        for (int h = 0; h < height; h++) {
            for (int w = 0; w < width; w++) {
                size_t idx = gray_idx(h, w);
                double threshold = get_threshold(h, w);
                binary_pixels[idx] = (grayscale[idx] < threshold) ? 0 : 255;
            }
        }
    }
};
void build_image_from_file() {
    // START GLOBAL TIME HERE
    start_time = chrono::high_resolution_clock::now();

    // const char* img_path = "/mnt/c/Users/sreddy/Desktop/test1.png"; // white
    // const char* img_path = "/mnt/c/Users/sreddy/Desktop/test2.png";
    // const char* img_path = "C:/Users/sredd/Desktop/test2.png";
    // const char* img_path = "C:/Users/sreddy/Desktop/qr1.png";
    // const char* img_path = "C:/Users/sreddy/Desktop/qr2.png";
    // const char* img_path = "/mnt/c/Users/sreddy/Desktop/qr1.png";
    // const char* img_path = "/Users/smpl/Desktop/qr1.png"; // blank
    const char* img_path = "/Users/smpl/Desktop/qr2.png"; // blank
    // const char* img_path = "/Users/smpl/Desktop/pix1.png"; // blank
    // const char* img_path = "/Users/smpl/Desktop/pix2.png"; // white
    // const char* img_path = "/Users/smpl/Desktop/test.png"; // has padding
    // const char* img_path = "/Users/smpl/Desktop/test2.png"; // no padding
    // const char* img_path = "/Users/smpl/Desktop/test3.png"; // color

    int width, height, channels;
    unsigned char* pixels = stbi_load(img_path, &width, &height, &channels, 0);
    if (pixels == nullptr) {
        fprintf(stderr, "%s:%d: Failed to load image: %s\n", __FILE__, __LINE__,
                img_path);
        exit(1);
    }
    Image image = Image(width, height, channels, pixels);
    printf("Image loaded: W=%d, H=%d, Channels=%d\n", width, height, channels);

    // Detect finder patterns & print them
    auto patterns = image.detect_patterns();

    printf("\nFound %d finder patterns:\n", (int)patterns.size());
    for (size_t i = 0; i < patterns.size(); i++) {
        printf("Pattern %zd: position: (%.1f, %.1f), detected %d times\n",
               i + 1, patterns[i].x, patterns[i].y, patterns[i].count);
    }
    printf("image.grayscale\n");
    for (int h = 0; h < height; h++) {
        for (int w = 0; w < width; w++) {
            printf("%3d ", (int)image.grayscale[image.gray_idx(h, w)]);
        }
        printf("\n");
    }
    printf("image.binary_pixels\n");
    for (int h = 0; h < height; h++) {
        for (int w = 0; w < width; w++) {
            printf("%3d ", (int)image.binary_pixels[image.gray_idx(h, w)]);
        }
        printf("\n");
    }
    /*
    */

    stbi_image_free(pixels); // free up the image, closes the fd
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
}

int main() {
    printf("hello world!\n");
    // read_input_from_api();
    start_time = chrono::high_resolution_clock::now();
    build_image_from_file();
    end_time = chrono::high_resolution_clock::now();
    auto diff = chrono::duration<double, milli>(end_time - start_time).count();
    printf("TOTAL TIME: %f ms\n", diff);
    // send_response_to_api();
    return 0;
}

