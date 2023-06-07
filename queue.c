#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <unistd.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct node_fifo
{
    void* data;
    struct node_fifo* next;
    struct node_fifo* prev;
    struct node_fifo* parent;
    int id;
} node_fifo;

typedef struct node_cnd
{
    cnd_t cond;
    node_fifo* p;
    struct node_cnd* next;
    struct node_cnd* prev;
} node_cnd;

typedef struct cnd_queue
{
    node_cnd* head;
    node_cnd* tail;
    node_cnd* nxt_deq;
    size_t waiting;

} cnd_queue;

typedef struct fifo_queue
{
    node_fifo* head;
    node_fifo* tail;
    cnd_queue cnd;
    size_t size;
    size_t visited;
} queue;

queue* q;
cnd_queue* cnd;
mtx_t mtx;
queue* ready_to_deq;
int cnt = 0;

void initQueue(void)
{
        ///This function will be called before the queue is used.
    ///It should initialize the queue and any other data structures
    q = malloc(sizeof(queue));
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
    q->visited = 0;
    cnd = malloc(sizeof(cnd_queue));
    cnd->head = NULL;
    cnd->tail = NULL;
    cnd->nxt_deq = NULL;
    cnd->waiting = 0;
    q->cnd = *cnd;
    mtx_init(&mtx, mtx_plain);
    ready_to_deq = malloc(sizeof(queue));
    ready_to_deq->head = NULL;
    ready_to_deq->tail = NULL;
    ready_to_deq->size = 0;
    ready_to_deq->visited = 0;
}

void destroyQueue(void)
{
    ///This function will be called when the queue is no longer needed.
    ///It should clean up any memory or other resources used by the queue.
    node_fifo* tmp;
    node_fifo* tmp2;
    node_cnd* cnd_tmp;
    node_cnd* cnd_tmp2;
    mtx_lock(&mtx);
    if (q->head != NULL)
    {
        tmp = q->head;
        while (tmp != NULL)
        {
            tmp2 = tmp;
            tmp = tmp->next;
            if (tmp2->data != NULL)
                free(tmp2->data);
            free(tmp2);
        }
    }
    free(q);
    if (cnd->head != NULL)
    {
        cnd_tmp = cnd->head;
        while (cnd_tmp != NULL)
        {
            cnd_tmp2 = cnd_tmp;
            cnd_tmp = cnd_tmp->next;
            cnd_destroy(&cnd_tmp2->cond);
            free(cnd_tmp2);
        }
    }
    free(cnd);
    if (ready_to_deq->head != NULL)
    {
        tmp = ready_to_deq->head;
        while (tmp != NULL)
        {
            tmp2 = tmp;
            tmp = tmp->next;
            free(tmp2);
        }
    }
    free(ready_to_deq);
    mtx_unlock(&mtx);
    mtx_destroy(&mtx);
}

void enqueue(void* data)
{        
    ///This function will be called by the producer threads.
    ///It should add the given data pointer to the queue.
    node_fifo* new_node = malloc(sizeof(node_fifo));
    node_fifo* tmp;
    mtx_lock(&mtx);
    new_node->data = data;
    new_node->id = cnt;
    cnt++;
    if (cnd->nxt_deq != NULL)
    {
        //there is a thread waiting to dequeue
        cnd->nxt_deq->p = new_node;
        cnd_signal(&(cnd->nxt_deq->cond));
        mtx_unlock(&mtx);
        return;
    }
    else
    {
        //no thread is waiting do dequeue
        if (q->head == NULL)
        {
            q->head = new_node;
            q->tail = new_node;
        }
        else
        {
            q->tail->next = new_node;
            new_node->prev = q->tail;
            q->tail = new_node;
        }
        q->size++;

        tmp = malloc(sizeof(node_fifo));
        tmp->parent = new_node;
        //abuse of notation but will work :)
        new_node->parent = tmp;
        if (ready_to_deq->head == NULL)
        {
            ready_to_deq->head = tmp;
            ready_to_deq->tail = tmp;
        }
        else
        {
            ready_to_deq->tail->next = tmp;
            tmp->prev = ready_to_deq->tail;
            ready_to_deq->tail = tmp;
        }
        mtx_unlock(&mtx);
        return;
    }
}

void* dequeue(void)
{
    printf("dequeue\n");
    node_cnd* new_node;
    node_fifo* tmp;
    node_fifo* ret;
    cnd_t c;
    cnd_init(&c);
    mtx_lock(&mtx);
    if(ready_to_deq->head == NULL)
    {
        //no item is waiting to be dequeued
        new_node = malloc(sizeof(node_cnd));
        new_node->p = NULL;
        new_node->next = NULL;
        new_node->prev = NULL;
        new_node->cond = c;
        printf("init new_node");
        if (cnd->head == NULL)
        {
            cnd->head = new_node;
            cnd->tail = new_node;
            cnd->nxt_deq = new_node;
        }
        else
        {
            cnd->tail->next = new_node;
            new_node->prev = cnd->tail;
            cnd->tail = new_node;
        }
        printf("waiting");
        cnd->waiting++;
        cnd_wait(&(new_node->cond), &mtx);
        ret = cnd->nxt_deq->p;//item in fifo
        tmp = ret->parent;//item in ready_to_deq
    }
    else
    {
        //there is an item waiting to be dequeued
        ret = ready_to_deq->head->parent;
        tmp = ret->parent;

    }
    //now i have an item to dequeue
    new_node = cnd->nxt_deq;
    if (tmp->prev == NULL)
    {
        ready_to_deq->head = tmp->next;
        if (tmp->next != NULL)
        {
            tmp->next->prev = NULL;
        }
        else
        {
            ready_to_deq->tail = NULL;
        }
    }
    else
    {
        tmp->prev->next = tmp->next;
        if (tmp->next != NULL)
        {
            tmp->next->prev = tmp->prev;
        }
        else
        {
            ready_to_deq->tail = tmp->prev;
        }
    }
    free(tmp);
    if (ret->next != NULL)
    {
        ret->next->prev = ret->prev;
    }
    else
    {
        q->tail = ret->prev;
    }
    q->head = ret->next;
    q->size--;
    q->visited++;
    void* data = ret->data;
    free(ret);
    if (new_node != NULL)
    {
        cnd->nxt_deq = cnd->nxt_deq->next;
        cnd->waiting--;
        if (new_node->prev != NULL)
        {
            new_node->prev->next = new_node->next;
        }
        else
        {
            cnd->head = new_node->next;
        }
        if (new_node->next != NULL)
        {
            new_node->next->prev = new_node->prev;
        }
        else
        {
            cnd->tail = new_node->prev;
        }
        cnd_destroy(&(new_node->cond));
        free(new_node);
    }
    mtx_unlock(&mtx);
    return data;
}

bool tryDequeue(void** ret)
{
    node_fifo* tmp = ready_to_deq->head;
    node_fifo* item;
    mtx_lock(&mtx);
    if (tmp == NULL)
    {
        mtx_unlock(&mtx);
        return false;
    }
    else
    {
        if (tmp->next != NULL)
        {
            tmp->next->prev = NULL;
        }
        else
        {
            ready_to_deq->tail = NULL;
        }
        ready_to_deq->head = tmp->next;
        item = tmp->parent;
        free(tmp);
        if (item->next != NULL)
        {
            item->next->prev = item->prev;
        }
        else
        {
            q->tail = NULL;
        }
        if (q->head->id == item->id)
        {
            q->head = item->next;
        }
        else
        {
            item->prev->next = item->next;
        }
        q->size--;
        q->visited++;
        *ret = item->data;
        free(item);
        mtx_unlock(&mtx);
        return true;
    }
}

size_t size(void)
{
    return q->size;
}

size_t waiting(void)
{
    size_t waiting;
    mtx_lock(&mtx);
    waiting = cnd->waiting;
    mtx_unlock(&mtx);
    return waiting;
}

size_t visited(void)
{
    return q->visited;
}

//int main()
// {
//     int items[] = {1, 2, 3, 4, 5};
//     size_t num_items = sizeof(items) / sizeof(items[0]);
//     int* item = malloc(sizeof(int));
//     initQueue();
//     for (size_t i = 0; i < num_items; i++)
//     {
//         enqueue(&items[i]);
//     }
//     for (size_t i = 0; i < num_items; i++)
//     {
//         printf("Size: %zu\n", size());
//         item = (int *)dequeue();
//         printf("Dequeued: %d\n", *item);
//         if(*item != items[i])
//         {
//             printf("dequeue test failed.\n");
//             return 1;
//         }
//     }
//     if((size() != 0))
//     {
//         printf("dequeue test failed.\n");
//         return 1;
//     }

//     destroyQueue();

//     printf("enqueue and dequeue test passed.\n");
//     return 0;

// }
