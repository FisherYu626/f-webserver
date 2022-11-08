#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;
    //读缓冲
    static const int READ_BUFFER_SIZE = 2048;
    //写缓冲
    static const int WRITE_BUFFER_SIZE = 1024;
    //请求数据的最大长度
    static const int QUERY_RANGE_MAX = 10485760;
    //报文请求方法
    enum METHOD
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    //主状态机状态
    enum CHECK_STATE
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    //报文解析结果
    enum HTTP_CODE
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    //从状态机状态
    enum LINE_STATUS
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {} 

public:
    //初始化套接字地址
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    //关闭http连接
    void close_conn(bool real_close = true);
    void process();
    //获取浏览器发送的全部数据
    bool read_once();
    //响应报文写入函数
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    //同步线程初始化数据库
    void initmysql_result(connection_pool *connPool);
    int timer_flag;
    int improv;


private:
    void init();
    //从m_read_buf读取并处理请求报文
    HTTP_CODE process_read();
    //向m_write_buf写入响应报文
    bool process_write(HTTP_CODE ret);
    //主状态机解析响应请求
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    //生成响应报文
    HTTP_CODE do_request();
    //m_start_line已解析的行数 返回指向的未处理字符
    char *get_line() { return m_read_buf + m_start_line; };
    //从状态机读取一行
    LINE_STATUS parse_line();
    void unmap();
    //响应报文生成相应部分 由do_request
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_bp_headers(int file_len,int m_content_s,int m_content_e);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_content_range(int start,int end,int content_len);
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;
    MYSQL *mysql;
    int m_state;  //读为0, 写为1

private:
    int m_sockfd;
    sockaddr_in m_address;
    //存储读取的请求报文
    char m_read_buf[READ_BUFFER_SIZE];
    //缓冲区中数据的最后一个字节的下一个位置
    int m_read_idx;
    //m_read_buf读取的位置m_checked_idx
    int m_checked_idx;
    //m_read_buf已经解析的字符个数
    int m_start_line;

    //响应数据报文存储的buffer
    char m_write_buf[WRITE_BUFFER_SIZE];
    //指示buffer长度
    int m_write_idx;

    // //断点重传响应报文存储的指针
    // char m_write_bp_buf[WRITE_BUFFER_SIZE];
    // //指示断点续传报文长度
    // int m_write_bp_idx;

    //主状态机状态
    CHECK_STATE m_check_state;
    //请求方法
    METHOD m_method;

    //解析请求报文中的变量
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    //指示请求范围start
    int m_content_s;
    //指示请求范围end
    int m_content_e;
    bool m_linger;
    //服务器上的文件地址 文件内存映射
    char *m_file_address;
    struct stat m_file_stat;
    //io向量
    struct iovec m_iv[2];
    int m_iv_count;
    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    //剩余发送字节数
    int bytes_to_send;
    //已发送字节数
    int bytes_have_send;
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode;
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
