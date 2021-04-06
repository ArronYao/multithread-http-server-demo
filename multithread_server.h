#ifndef __SERVER_H__
#define __SERVER_H__

#include<iostream>
#include<sys/stat.h>
#include<fcntl.h>
#include <sys/types.h>
#include "tools.h"
#include "sbuf.h"
#include <sys/wait.h>


using namespace std;

void m_handle_req(int fd);
// generate http response to client
void m_client_error(int fd, const char* cause, const char* errnum, const char* shortmsg, const char* longmsg);

bool parse_uri(char* uri, char* filename, char* args);

void m_serve_static(const char* method, int fd, char * filename);

void m_serve_dynamic(const char* method, int fd, char * filename, char* args);

bool check_validation(int fd, const char* filename, struct stat * pfile_state, const char* method, bool is_static);

void getMIMEType(const char* filename, char* type);

void* thread(void* vargp);
 
#endif
