#ifndef MINIX_HTTP_H
#define MINIX_HTTP_H

#include "inet.h"

  typedef struct http_parser http_parser;

  #define HTTP_DEFAULT_PORT 80

  PUBLIC const char *TEXT_PLAIN = "text/plain";
  PUBLIC const char *OCTET_STREAM = "application/octet-stream";

  struct http_parsed_url
  {
    char host[100];
    size_t host_s;

    unsigned int port;

    char file[255];
    size_t file_s;

    unsigned int errorno;
  };

  PUBLIC int http_parse_url(const char *url, size_t url_s, struct http_parsed_url *parsed_url);

  PUBLIC int http_connect_url(struct http_parsed_url *url);

  struct http_parser
  {
    unsigned int nread;
    unsigned int content_length;

    unsigned short http_major;
    unsigned short http_minor;
    unsigned int status_code : 16;
    unsigned int method : 8;
    unsigned int errorno : 7;

    unsigned int upgrade : 1;

    void *connection;
  };

#endif //MINIX_HTTP_H
