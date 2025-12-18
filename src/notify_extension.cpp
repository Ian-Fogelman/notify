#define DUCKDB_EXTENSION_MAIN

#include "notify_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

// Notify
#include <iostream>
#include <string>
#include <curl/curl.h>

namespace duckdb {

// Callback function to capture response data
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	((std::string *)userp)->append((char *)contents, size * nmemb);
	return size * nmemb;
}

inline void SlackWebhookScalarFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &webhook_url_vector = args.data[0];
	auto &message_vector = args.data[1];
	auto &auth_token_vector = args.data[2];

	TernaryExecutor::Execute<string_t, string_t, string_t, string_t>(
	    webhook_url_vector, message_vector, auth_token_vector, result, args.size(),
	    [&](string_t webhook_url, string_t message, string_t auth_token) {
		    CURL *curl;
		    CURLcode res;
		    std::string response_string;
		    std::string error_msg;

		    // Create JSON payload
		    std::string json_payload = R"({"text": ")" + message.GetString() + R"("})";

		    // Initialize curl for this request
		    curl = curl_easy_init();

		    if (curl) {
			    // Set the target URL
			    curl_easy_setopt(curl, CURLOPT_URL, webhook_url.GetString().c_str());

			    // Specify the POST data
			    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_payload.c_str());

			    // Set HTTP headers
			    struct curl_slist *headers = NULL;
			    headers = curl_slist_append(headers, "Content-Type: application/json");

			    // Add Authorization header if token is provided and not empty
			    std::string token_str = auth_token.GetString();
			    if (!token_str.empty()) {
				    std::string auth_header = "Authorization: Bearer " + token_str;
				    headers = curl_slist_append(headers, auth_header.c_str());
			    }

			    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

			    // Set callback to capture response
			    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
			    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);

			    // Set timeout to avoid hanging
			    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

			    // Perform the request
			    res = curl_easy_perform(curl);

			    // Check for errors
			    if (res != CURLE_OK) {
				    error_msg = "Error: " + std::string(curl_easy_strerror(res));
			    } else {
				    long response_code;
				    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
				    if (response_code >= 200 && response_code < 300) {
					    error_msg = "Success (HTTP " + std::to_string(response_code) + ")";
				    } else {
					    error_msg = "Failed (HTTP " + std::to_string(response_code) + "): " + response_string;
				    }
			    }

			    // Clean up headers and curl handle
			    curl_slist_free_all(headers);
			    curl_easy_cleanup(curl);
		    } else {
			    error_msg = "Error: Failed to initialize libcurl";
		    }

		    return StringVector::AddString(result, error_msg);
	    });
}

static void LoadInternal(ExtensionLoader &loader) {
	curl_global_init(CURL_GLOBAL_DEFAULT);
	auto slack_webhook_function =
	    ScalarFunction("notify_slack", {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	                   LogicalType::VARCHAR, SlackWebhookScalarFun);
	loader.RegisterFunction(slack_webhook_function);
}

void NotifyExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}
std::string NotifyExtension::Name() {
	return "notify";
}

std::string NotifyExtension::Version() const {
#ifdef EXT_VERSION_NOTIFY
	return EXT_VERSION_NOTIFY;
#else
	return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(notify, loader) {
	duckdb::LoadInternal(loader);
}
}
