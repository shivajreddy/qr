#include "api.h"
#include <curl/curl.h>
#include <optional>

using std::cerr;
using std::optional;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    auto* str = static_cast<string*>(userp);
    str->append(static_cast<char*>(contents), total_size);
    return total_size;
}

optional<string> curl_get(string URL) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return std::nullopt;

    string response;

    curl_easy_setopt(curl, CURLOPT_URL, URL.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode curl_code = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (curl_code != CURLE_OK) {
        return std::nullopt;
    }

    return response;
}

// optional<string> save_image(string image_url) {
void save_image(string image_url) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        cerr << "Failed to initiate CURL\n";
        return;
    }

    string response;
    curl_easy_setopt(curl, CURLOPT_URL, image_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res_code = curl_easy_perform(curl);
    if (res_code != CURLE_OK) {
        cerr << "Failed to perform GET\n";
        return;
    }
    curl_easy_cleanup(curl);

    // Write to file in binary mode
    FILE* image = nullptr;
    image = fopen("image.png", "wb"); // write mode, binary mode
    if (image == nullptr) {
        cerr << "Failed to open image.png\n";
        exit(1);
    }

    fwrite(response.data(), sizeof(char), response.size(), image);
    curl_get(image_url);

    fclose(image);
}

void api_post_data(const char* POST_URL, string json_str) {
    CURL* curl = curl_easy_init();
    if (!curl) return;

    curl_easy_setopt(curl, CURLOPT_URL, POST_URL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    std::string response;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res)
                  << std::endl;
        return;
    }

    std::cout << "Response:\n" << response << std::endl;
}
