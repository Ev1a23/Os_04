#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct node_fifo
{
    void* data; //for item queue it will be void*, for cnd it will be cnd_t*
    struct node_fifo* next;
    struct node_fifo* prev;
    struct node_fifo* parent;
} node;

typedef struct queue
{
    node* head;
    node* tail;
    int size;
} queue;

queue* fifo_q;
queue* cnd_q;
mtx_t mtx;
queue* ready_q;
node* last_signaled_p;

atomic_int visited_cnt;
atomic_int wait_cnt;

void* dequeue_ll(queue* q)
{
    void* p;
    node* n = q->head;
    q->head = q->head->next;
    if(q->head == NULL)
    {
        q->tail = NULL;
    }
    else
    {
        q->head->prev = NULL;
    }
    p = n->data;
    free(n);
    q->size--;
    return p;
}

void* remove_node_from_list(queue* q, node* n)
{
    void* p;
    if (n->prev == NULL) //n is head of list
    {
        return dequeue_ll(q);
    }
    if(n->next == NULL)
    {
        p = n->data;
        q->tail = n->prev;
        q->tail->next = NULL;
        free(n);
        q->size--;
        return p;
    }
    p = n->data;
    n->prev = n->prev->prev;
    n->next  = n->next ->next;
    free(n);
    q->size--;
    return p;
}

void* enqueue_ll(queue* q, void* data)
{
    node* p = malloc(sizeof(node));
    p->data = data;
    p->next = NULL;
    p->prev = q->tail;
    if(q->tail == NULL)
    {
        q->head = p;
    }
    else
    {
        q->tail->next = p;
    }
    q->tail = p;
    q->size++;
    return p;
}

void* init_ll()
{
    queue* q = malloc(sizeof(queue));
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    return q;
}

size_t size(void)
{
    return fifo_q->size;
}

size_t waiting(void)
{
    mtx_lock(&mtx);
    size_t w;
    w = (size_t) wait_cnt;
    mtx_unlock(&mtx);
    return w;
}

size_t visited()
{
    return (size_t) visited_cnt;
}

void initQueue(void)
{
    ///This function will be called before the queue is used.
    ///It should initialize the queue and any other data structures
    mtx_init(&mtx, mtx_plain);
    fifo_q = init_ll();
    cnd_q = init_ll();
    ready_q = init_ll();
    visited_cnt = 0;
    wait_cnt = 0;
    enqueue_ll(cnd_q, (node*) malloc(sizeof(node))); //just a sentinel
    last_signaled_p = cnd_q->head;
}

void destroyQueue(void)
{
    ///This function will be called when the queue is no longer needed.
    ///It should clean up any memory or other resources used by the queue.
    mtx_destroy(&mtx);
    while(fifo_q->head!=NULL)
    {
        dequeue_ll(fifo_q);
    }
    free(fifo_q);

    while(cnd_q->head!=NULL)
    {
        dequeue_ll(cnd_q);
    }
    free(cnd_q);

    while(ready_q->head!=NULL)
    {
        dequeue_ll(ready_q);
    }
    free(ready_q);
}

void enqueue(void* data)
{
    ///This function should add the data to the queue
    mtx_lock(&mtx);
    node* n = enqueue_ll(fifo_q, data);
    
    if(last_signaled_p->next == NULL)
    {
        enqueue_ll(ready_q, n);
    }
    else
    {
        last_signaled_p = last_signaled_p->next;
        last_signaled_p ->parent = n;
        cnd_signal((cnd_t *) last_signaled_p->data);
    }
    mtx_unlock(&mtx);
}

void* dequeue()
{
    mtx_lock(&mtx);
    void* data;
    if(ready_q->head == NULL)
    {
        //there is no item ready to dequeue
        node* n;
        cnd_t cnd;
        cnd_init(&cnd);
        n = enqueue_ll(cnd_q, &cnd);
        wait_cnt++;
        cnd_wait(&cnd, &mtx);
        wait_cnt--;
        data = remove_node_from_list(fifo_q, n->parent);
        cnd_destroy(&cnd);
        visited_cnt++; 
        mtx_unlock(&mtx);
        return data;
    }
    else
    {
        data = remove_node_from_list(fifo_q, dequeue_ll(ready_q));
        visited_cnt++;
        mtx_unlock(&mtx);
        return data;
    }
}

bool tryDequeue(void** point)
{
    mtx_lock(&mtx);
    if(ready_q->head == NULL)
    {
        mtx_unlock(&mtx);
        return false;
    }
    *point = remove_node_from_list(fifo_q, dequeue_ll(ready_q));
    visited_cnt++;
    mtx_unlock(&mtx);
    return true;
}


