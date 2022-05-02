#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

#define QUEUE_LENGTH 20
#define EXIT exit(EXIT_FAILURE)

#define ANY_METHOD_REQLINE_REGEX "\\S+ \\/[a-zA-Z0-9\\.\\-\\/]* HTTP\\/1\\.1"
#define CORRECT_METHOD_REQLINE_REGEX "(GET|HEAD) \\/[a-zA-Z0-9\\.\\-\\/]* HTTP\\/1\\.1"
#define ANY_HEADERLINE_REGEX "\\S+\\: *\\S+ *"
#define CRLF "\r\n"

char* string_to_pchar(std::string str) {
    char *res = new char[str.size() + 1];

    for (size_t i = 0; i < str.size(); ++i) {
        res[i] = str.at(i);
    }
    res[str.size()] = 0;

    return res;
}
void respond(int msg_sock, std::string status_code, std::string reason_phrase, std::string header = "", std::string body = "") {
    std::string response_message = "HTTP/1.1 " + status_code + " " + reason_phrase + CRLF;
    if (header != "") {
        response_message += header + CRLF;
    }
    response_message += CRLF;
    if (body != "") {
        response_message += body;
    }

    ssize_t len = response_message.size() + 1;
    char *buffer = string_to_pchar(response_message);

    if (write(msg_sock, buffer, len - 1) != len - 1) {
        EXIT;
    }
}
void prepare_statuscode200_response(int msg_sock, std::string path_to_file, bool include_body) {
    std::ifstream file(path_to_file, std::ios::binary | std::ios::ate);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    file.read(buffer.data(), size);

    std::string body(buffer.begin(), buffer.end());

    std::stringstream ss;
    ss << size;
    std::string str = ss.str();

    respond(msg_sock, "200", "SUCCESS", "Content-Length: " + str +
            (include_body ? std::string(CRLF) + "Content-Type: application/octet-stream" : ""),
            include_body ? body : "");
}
bool file_found_in_correlated(int msg_sock, std::string correlated_servers, std::string path_to_file) {
    std::ifstream fs(correlated_servers);
    std::string path, server, port;

    while (!fs.eof()) {
        fs >> path >> server >> port;

        if (path == path_to_file) {
            respond(msg_sock, "302", "File temporarily moved.",
                    "Location: http://" + server + ":" + port + " " + path);
            return true;
        }
    }

    return false;
}
void uncapitalize_letters(std::string* str) {
    int len = str->size();

    for (int i = 0; i < len; ++i) {
        int num = int(str->at(i));

        if (65 <= num && num <= 90) {
            std::string tmp = "";
            tmp += char(num + 32);
            str->replace(i, 1, tmp);
        }
    }
}
bool isNotZero(std::string str) {
    size_t len = str.size();

    for (size_t i = 0; i < len; ++i) {
        if (int(str.at(i)) != 48) {
            return true;
        }
    }

    return false;
}
void read_rest_of_request(FILE* tcp_socket) {
    char *line;
    ssize_t len;
    size_t ll = 0;

    for(;;) {
        line = NULL;
        len = getline(&line, &ll, tcp_socket);

        if (len <= 2) {
            break;
        }
    }
}
void read_rest_of_stream(FILE* tcp_socket) {
    char *line;
    ssize_t len;
    size_t ll = 0;

    for(;;) {
        line = NULL;
        len = getline(&line, &ll, tcp_socket);

        if (len <= 0) {
            break;
        }
    }

    fclose(tcp_socket);
}
bool is_a_subpath(fs::path subpath, fs::path path) {
    subpath = fs::absolute(subpath.lexically_normal());
    path = fs::absolute(path.lexically_normal());

    std::string substr{subpath.u8string()};
    std::string str{path.u8string()};

    size_t lensub = substr.size();
    size_t lenstr = str.size();

    if (lensub > lenstr) {
        return false;
    }

    return substr == str.substr(0, lensub);
}
void handle_client(int msg_sock, std::string main_directory, std::string correlated_servers) {
    char *line;
    ssize_t len;
    size_t ll = 0;
    bool close_connection = false;

    FILE* tcp_socket = fdopen(msg_sock, "r");

    //reading requests
    for (;;) {
        bool connection_headers, type_headers, length_headers, server_headers;
        connection_headers = type_headers = length_headers = server_headers = false;
        line = NULL;
        len = getline(&line, &ll, tcp_socket);

        //nothing left to read
        if (len == -1) {
            break;
        }


        if (line == NULL) {
            respond(msg_sock, "500", "Server issue.", "Connection: close");
            read_rest_of_stream(tcp_socket);
            return;
        } else if (line[len - 2] != 13) {
            respond(msg_sock, "400", "Wrong input. Lack of CR.", "Connection: close");
            read_rest_of_stream(tcp_socket);
            return;
        }

        line[len - 1] = line[len - 2] = 0;
        len -= 2;

        if (!std::regex_match(line, std::regex(ANY_METHOD_REQLINE_REGEX))) {
            respond(msg_sock, "400", "Wrong request line.", "Connection: close");
            read_rest_of_stream(tcp_socket);
            return;
        } else if (!std::regex_match(line, std::regex(CORRECT_METHOD_REQLINE_REGEX))) {
            respond(msg_sock, "501", "Wrong method.");
            read_rest_of_request(tcp_socket);
            continue;
        }

        //request-line is correct
        std::string method, path, ver;
        char splitter[] = " :";
        char* tmp = strtok(line, splitter);
        method = tmp;
        tmp = strtok(NULL, splitter);
        path = tmp;
        tmp = strtok(NULL, splitter);
        ver = tmp;

        //reading headers
        for (;;) {
            line = NULL;
            len = getline(&line, &ll, tcp_socket);

            //headers have to be followed by CRLF line
            if (len == -1 || line[len - 2] != 13) {
                respond(msg_sock, "400", "Wrong input. Lack of CR.", "Connection: close");
                read_rest_of_stream(tcp_socket);
                break;
            } else if (len == 2) {
                break;
            }

            line[len - 2] = line[len - 1] = 0;
            len -= 2;

            if (!std::regex_match(line, std::regex(ANY_HEADERLINE_REGEX))) {
                respond(msg_sock, "400", "Wrong header.", "Connection: close");
                read_rest_of_stream(tcp_socket);
                return;
            }

            std::string header, value;
            tmp = NULL;
            tmp = strtok(line, splitter);
            header = tmp;
            tmp = strtok(NULL, splitter);
            value = tmp;

            uncapitalize_letters(&header);

            if (header == "connection") {
                if (connection_headers) {
                    respond(msg_sock, "400", "Repeating header \"connection\".", "Connection: close");
                    read_rest_of_stream(tcp_socket);
                    return;
                }
                connection_headers = true;

                if (value == "close") {
                    close_connection = true;
                }
            } else if (header == "content-type") {
                if (type_headers) {
                    respond(msg_sock, "400", "Repeating header \"content-type\".", "Connection: close");
                    read_rest_of_stream(tcp_socket);
                    return;
                }
                type_headers = true;
            } else if (header == "content-length") {
                if (length_headers) {
                    respond(msg_sock, "400", "Repeating header \"content-length\".", "Connection: close");
                    read_rest_of_stream(tcp_socket);
                    return;
                }
                length_headers = true;

                if (isNotZero(value)) {
                    respond(msg_sock, "400", "Request cannot have content-length bigger than 0.", "Connection: close");
                    read_rest_of_stream(tcp_socket);
                    return;
                }
            } else if (header == "server") {
                if (server_headers) {
                    respond(msg_sock, "400", "Repeating header \"server\".", "Connection: close");
                    read_rest_of_stream(tcp_socket);
                    return;
                }
                server_headers = true;
            }
        }

        fs::path path_to_target = main_directory;
        path_to_target += path;

        if (!is_a_subpath(fs::path(main_directory), path_to_target)) {
            respond(msg_sock, "404", "Cannot access outside of given directory.");
        } else if (fs::exists(path_to_target) && !fs::is_directory(path_to_target)) {
            std::string entire_path = main_directory + path;
            prepare_statuscode200_response(msg_sock, entire_path, method == "GET");
        } else if (fs::exists(path_to_target) && fs::is_directory(path_to_target)) {
            respond(msg_sock, "404", "Target cannot be a folder.");
            read_rest_of_stream(tcp_socket);
            return;
        } else if (!file_found_in_correlated(msg_sock, correlated_servers, path)) {
            respond(msg_sock, "404", "File not found.");
        }

        if (close_connection == true) {
            break;
        }
    }

    fclose(tcp_socket);
}

int main(int argc, char *argv[])
{
    uint32_t PORT_NUM = 8080;
    int sock, msg_sock;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    if (argc < 3 || 4 < argc) {
        std::cerr << "Usage " << argv[0] << "folder_with_files file_with_correlated_servers (port)\n";
        EXIT; 
    }

    std::string main_directory = argv[1];
    std::string correlated_servers = argv[2];

    //checking if given paths exist
    if (!fs::exists(fs::path(main_directory)) || !fs::exists(fs::path(correlated_servers))) {
        EXIT;
    }

    if (argc == 4) {
        PORT_NUM = atoi(argv[3]);
    }

    //preparing server
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        EXIT;
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(PORT_NUM);

    if (bind(sock, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) {
        EXIT;
    }
    
    if (listen(sock, QUEUE_LENGTH) < 0) {
        EXIT;
    }

    std::cerr << "accepting client connections on port " << ntohs(server_address.sin_port) << "\n";
    for (;;) {
        client_address_len = sizeof(client_address);

        msg_sock = accept(sock, (struct sockaddr *) &client_address, &client_address_len);

        std::cerr << "new connection\n";

        if (msg_sock < 0) {
            EXIT;
        }

        handle_client(msg_sock, main_directory, correlated_servers);
        
        std::cerr << "ending connection\n";
    }

    return 0;
}
