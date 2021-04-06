#include "multithread_server.h"

const int MAXTHREADS = 4;
const int SBUFSIZE = 16;
const int MAXLINE = 8192;
sbuf_t sbuf;

int main(int argc, char ** argv){
    if (argc != 2){
        cerr << "usage: " << argv[0] << " [port]" << endl;
        exit(0);
    }

    int listenfd = open_listenfd(argv[1]);
    // char portt[10];
    // strcpy(portt, "8000");
    // int listenfd = open_listenfd(portt);
    if (listenfd < 0){
        cerr << "Create listen socket failed." << endl;
        exit(0);
    }

    pthread_t tids[MAXTHREADS];
    sbuf_init(&sbuf, SBUFSIZE);
    for(int i = 0; i < MAXTHREADS; i++)
        Pthread_create(tids+i, nullptr, thread, nullptr);


    struct sockaddr_storage client_socket_addr;
    socklen_t sock_len;
    char hostname[MAXLINE], port[MAXLINE];
    while(1){
        sock_len = sizeof(client_socket_addr);
        int connfd = Accept(listenfd, (sockaddr *) &client_socket_addr, &sock_len);

        if (getnameinfo((sockaddr *) &client_socket_addr, sock_len, 
            hostname, MAXLINE, port, MAXLINE, NI_NUMERICHOST | NI_NUMERICSERV) < 0){
                cerr << "getnameinfo error" << strerror(errno) << endl;
                exit(0);
            }
        cout << "connected to " << hostname << " : " << port << endl;
        
        /****** multithread version ******/
        sbuf_insert(&sbuf, connfd);
    }
    return 0;
}

void m_handle_req(int fd){
    rio_t rt;
    char buf[MAXLINE];
    Rio_readinitb(&rt, fd);

    // read and parse the http request line
    char method[10], uri[1000], version[10];
    Rio_readlineb(&rt, buf, MAXLINE);
    if (buf[0] == '\0')
        return ;
    else
        sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "get")){
        m_client_error(fd, method, "501", "Not implemented", "This method is not implemented yet");
        return ;
    }

    char filename[MAXLINE], args[MAXLINE];
    bool is_static = parse_uri(uri, filename, args);

    if (is_static)
        m_serve_static(method, fd, filename);
    else
        m_serve_dynamic(method, fd, filename, args);
}

void m_client_error(int fd, const char* cause, const char* errnum, const char* shortmsg, const char* longmsg){
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    // single thread version
    // sprintf(buf, "<hr><em>The Demo Web server.</em>\r\n");
    // Rio_writen(fd, buf, strlen(buf));

    // multithread version
    sprintf(buf, "<hr><em>The Demo Web server. Thread: %u. </em>\r\n", (unsigned int)pthread_self());
    Rio_writen(fd, buf, strlen(buf));

}

bool parse_uri(char* uri, char* filename, char* args){
    bool is_static;
    char* pcgi = strstr(uri, "cgi-bin");
    if (pcgi == nullptr){
        is_static = true;
        strcpy(filename, "");
        strcat(filename, ".");
        strcat(filename, uri);
        if (uri[strlen(uri) - 1] == '/')
            strcpy(filename, "home.html");
    }
    else{
        is_static = false;

        char* p = strstr(uri, "?");
        if (p != nullptr){
            strcpy(args, "");
            args = strcpy(args, p+1);
            *p = '\0';
        }
        else
            strcpy(args, "");

        strcpy(filename, ".");
        filename = strcat(filename, uri);
    }

    return is_static;
}

void m_serve_static(const char* method, int fd, char * filename){
    struct stat file_state;
    if (! check_validation(fd, filename, &file_state, method, true))
        return ;

    char buf[MAXLINE], filetype[MAXLINE];

    // generate the response header
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Web server demo. Thread: %u.\r\n", (unsigned int)pthread_self());
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n", (int) file_state.st_size);
    Rio_writen(fd, buf, strlen(buf));
    getMIMEType(filename, filetype);
    sprintf(buf, "Content-type: %s\r\n\r\n", filetype);
    Rio_writen(fd, buf, strlen(buf));
    
    // generate the response body
    int srcfd = Open(filename, O_RDONLY, 0);
    void * srcp = (void*) Mmap(0, file_state.st_size, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);
    Rio_writen(fd, srcp, file_state.st_size);
    Munmap(srcp, file_state.st_size);

}

void m_serve_dynamic(const char* method, int fd, char * filename, char* args){
    struct stat file_state;
    if (! check_validation(fd, filename, &file_state, method, false))
        return ;

    char buf[MAXLINE], *empty_list[] ={NULL};

    // generate the first part of the http response
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Web server demo. Thread: %u.\r\n", (unsigned int)pthread_self());
    Rio_writen(fd, buf, strlen(buf));

    if (Fork() == 0){
        setenv("QUERY_STRING", args, 1);
        Dup2(fd, STDOUT_FILENO);
        Execve(filename, empty_list, environ);
    }
    wait(NULL);
}

bool check_validation(int fd, const char* filename, struct stat * pfile_state, const char* method, bool is_static){
    if (stat(filename, pfile_state) < 0){
        m_client_error(fd, filename, "404", "File not found", "The server can not find the requested file.");
        return false;
    }

    if (is_static){
        if (!S_ISREG(pfile_state->st_mode) || !(S_IRUSR & pfile_state->st_mode)){
            m_client_error(fd, filename, "403", "Permission denied.", "The server has not the perimission to  access the file.");
            return false;
        }
    }
    else{
        if (!S_ISREG(pfile_state->st_mode) || !(S_IXUSR & pfile_state->st_mode)){
            m_client_error(fd, filename, "403", "Permission denied.", "The server has not the perimission to  run the file.");
            return false;
        }
    }

    return true;
}

void getMIMEType(const char* filename, char* type){
    if (strstr(filename, "html"))
        strcpy(type, "text/html");
    else if (strstr(filename, "gif"))
        strcpy(type, "image/gif");
    else if (strstr(filename, "png"))
        strcpy(type, "image/png");
    else if (strstr(filename, "jpg"))
        strcpy(type, "image/jpg");
    else
        strcpy(type, "text/plain");
}

void* thread(void* vargp){
    pthread_t pid = Pthread_self();
    Pthread_detach(pid);

    while (1)
    {
        int connfd = sbuf_remove(&sbuf);
        m_handle_req(connfd);
        Close(connfd);
        cout << "connection closed" << endl;
    }
    
}