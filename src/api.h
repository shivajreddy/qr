#ifndef GET_DATA_H
#define GET_DATA_H

#include <iostream>
#include <optional>
#include <string>

using std::cout;
using std::endl;
using std::optional;
using std::string;

// string get_data(const char* URL);
optional<string> curl_get(string URL);
void save_image(string image_url);

void post_response();

void api_post_data(const char* POST_URL, string json_str);

#endif // !GET_DATA_H
