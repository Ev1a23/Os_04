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
    struct node_fifo* parent; // to point to another nodes in other ll
} node;

typedef struct queue
{
    node* head;
    node* tail;
    size_t size;
} queue;

queue* fifo_q;
queue* cnd_q;
mtx_t mtx;
queue* ready_q;
node* sig_p;

atomic_int visited_cnt;
atomic_int waiting_cnt;

void* dequeue_ll(queue* q)
{
    // Help method to dequeue the first item from the given linked list (ll) and update pointers accordingly
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
    // Remove a specific node from the linked list (ll) and return the data stored in the node
    void* p;
    if (n->prev == NULL) //n is head of list
    {
        return dequeue_ll(q);
    }
    p = n->data;
    q->size--;
    if(n->next == NULL)
    {
        q->tail = n->prev;
        q->tail->next = NULL;
        free(n);   
        return p;
    }
    n->prev = n->prev->prev;
    n->next  = n->next ->next;
    free(n);
    return p;
}

void* enqueue_ll(queue* q, void* data)
{
    // Add a new node with the given data to the tail of the linked list (ll) and return the newly created node
    node* p = malloc(sizeof(node));
    p->data = data;
    p->prev = q->tail;
    p->next = NULL;
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
    // Initialize a new linked list (ll) and return a pointer to the created list
    queue* q = malloc(sizeof(queue));
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    return q;
}

size_t size(void)
{
    // Return the current size of the FIFO queue
    return fifo_q->size;
}

size_t waiting(void)
{
    // Return the number of threads waiting in the FIFO queue
    mtx_lock(&mtx);
    size_t w;
    w = (size_t) waiting_cnt;
    mtx_unlock(&mtx);
    return w;
}

size_t visited()
{
    // Return the number of items dequeued from the FIFO queue
    return (size_t) visited_cnt;
}

void initQueue(void)
{
    // Initialize the FIFO queue and other data structures
    mtx_init(&mtx, mtx_plain);
    fifo_q = init_ll();
    cnd_q = init_ll();
    ready_q = init_ll();
    visited_cnt = 0;
    waiting_cnt = 0;
    enqueue_ll(cnd_q, (node*) malloc(sizeof(node))); //just a sentinel
    sig_p = cnd_q->head;
}

void destroyQueue(void)
{
    // Clean up the memory and resources used by the FIFO queue
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
    // Add the data to the FIFO queue
    mtx_lock(&mtx);
    node* n = enqueue_ll(fifo_q, data);
    
    if(sig_p->next == NULL)
    {
        enqueue_ll(ready_q, n);
    }
    else
    {
        sig_p = sig_p->next;
        sig_p ->parent = n;
        cnd_signal((cnd_t *) sig_p->data);
    }
    mtx_unlock(&mtx);
}

void* dequeue()
{
    // Remove and return an item from the FIFO queue
    mtx_lock(&mtx);
    void* data;
    if(ready_q->head == NULL)
    {
        //there is no item ready to dequeue
        node* n;
        cnd_t cnd;
        cnd_init(&cnd);
        n = enqueue_ll(cnd_q, &cnd);
        waiting_cnt++;
        cnd_wait(&cnd, &mtx);
        //now i have an item to dequeue
        waiting_cnt--;
        data = remove_node_from_list(fifo_q, n->parent);
        cnd_destroy(&cnd);
        visited_cnt++; 
        mtx_unlock(&mtx);
        return data;
    }
    else
    {
        //there is an item ready to dequeue
        data = remove_node_from_list(fifo_q, dequeue_ll(ready_q));
        visited_cnt++;
        mtx_unlock(&mtx);
        return data;
    }
}

bool tryDequeue(void** point)
{
    // Try to remove and return an item from the FIFO queue, return false if the queue is empty, and true if an item was dequeued
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
