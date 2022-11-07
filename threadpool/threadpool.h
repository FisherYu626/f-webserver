#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T>
class threadpool
{
public:
    /*thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁
    sem m_queuestat;            //是否有任务需要处理
    connection_pool *m_connPool;  //数据库
    int m_actor_model;          //模型切换
};
template <typename T>
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number),
m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{   
    //参数检查
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    //线程池数组初始化
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    
    for (int i = 0; i < thread_number; ++i)
    {   
        //创建执行worker函数的线程 this为传递进线程的参数
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads;
            throw std::exception();
        }
        //分离当前线程 不用单独对当前线程进行回收
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}
template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}

//通过list容器创建请求队列，向队列中添加时，通过互斥锁保证线程安全，
//添加完成后通过信号量提醒有任务要处理，最后注意线程同步。
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    //保护请求队列的互斥锁
    m_queuelocker.lock();
    //超出容量 无法添加新请求到请求队列
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    //请求队列中添加请求
    m_workqueue.push_back(request);

    m_queuelocker.unlock();
    //是否有任务需要处理的信号量 此处信号量+1
    m_queuestat.post();
    return true;
}
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}
template <typename T>
void *threadpool<T>::worker(void *arg)
{   
    //获取当前线程池的地址
    threadpool *pool = (threadpool *)arg;
    //执行一个工作线程
    pool->run();
    return pool;
}

//工作线程从请求队列中取出某个任务进行处理，注意线程同步
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {   
        //信号量等待
        m_queuestat.wait();

        //被唤醒后先加互斥锁
        m_queuelocker.lock();
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }

        //从请求队列中取出第一个任务
        //将任务从请求队列删除
        T *request = m_workqueue.front();
        m_workqueue.pop_front();

        m_queuelocker.unlock();
        
        if (!request)
            continue;
        if (1 == m_actor_model)
        {   
            //根据请求状态进行响应
            if (0 == request->m_state)
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    //从连接池中取出一个数据库连接
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    //process(模板类中的方法,这里是http类)进行处理
                    request->process();
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
