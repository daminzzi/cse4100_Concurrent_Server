/* 
 * echoserveri.c - An iterative echo server 
 */ 
/* $begin echoserverimain */
#include "csapp.h"

typedef struct item {
    int id;
    int left_stock;
    int price;
    int readcnt;
    sem_t mutex;
    struct item* left;
    struct item* right;
}item;

typedef struct pool {
    int maxfd;
    fd_set read_set;
    fd_set ready_set;
    int nready;
    int maxi;
    int clientfd[FD_SETSIZE];
    rio_t clientrio[FD_SETSIZE];
}pool;

item* root;
int byte_cnt = 0;
char str[MAXLINE];

item* insert(item* root, int id, int left_stock, int price);
item* fMin(item* root);
item* search(item* root, int id);
item* delete(item* root, int id);
void preorderPrint(item* root);
void stockTxt(item* root, FILE* fp);
void freeTree(item* root);

void show(int connfd, item* node);
void buy(int connfd, item* node, int ID, int n);
void sell(int connfd, item* node, int ID, int n);

void init_pool(int listenfd, pool* p);
void add_client(int connfd, pool* p);
void check_clients(pool* p);

void echo(int connfd);

void sigint_handler(int signo) { // signal handler for sigint
    //printf("Terminate server\n");
    int r = remove("stock.txt"); // remove file
    if (r == -1) printf("Error : fail to remove stock.txt\n");
    else { // create stock.txt file
        FILE* fp2 = fopen("stock.txt", "w");
        if (fp2 == NULL) {
            perror("Failed to open the source");
        }
        else {
            stockTxt(root, fp2);
            fclose(fp2);
        }
    }
    freeTree(root);
    exit(1);
}

int main(int argc, char **argv) 
{
    Signal(SIGINT, sigint_handler); // check signal like ctrl + c
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */  //line:netp:echoserveri:sockaddrstorage
    char client_hostname[MAXLINE], client_port[MAXLINE];
    static pool pool;

    int id, left, price;
    FILE* fp = fopen("stock.txt", "r");
    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(0);
    }

    if (fp == NULL) {
        printf("Error: stock.txt file does not exist.\n");
        return 0;
    }
    else {
        while (EOF != fscanf(fp, "%d %d %d\n", &id, &left, &price)) {
            root = insert(root, id, left, price);
        }
        fclose(fp);
    }

    listenfd = Open_listenfd(argv[1]);
    init_pool(listenfd, &pool);

    while (1) {
	    /*Wait for listening/connected descriptor(s) to become ready*/
        pool.ready_set = pool.read_set;
        pool.nready = Select(pool.maxfd + 1, &pool.ready_set, NULL, NULL, NULL);

        /*If listening descriptor ready, add new client to pool*/
        if (FD_ISSET(listenfd, &pool.ready_set)) {
            clientlen = sizeof(struct sockaddr_storage);
            connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
            Getnameinfo((SA*)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
            printf("Connected to (%s, %s)\n", client_hostname, client_port);
            add_client(connfd, &pool);
        }
       
        /*Echo a text line from each ready connected descriptor*/
        check_clients(&pool);
    }

    exit(0);
}
/* $end echoserverimain */

item* insert(item* root, int id, int left_stock, int price) {
    if (root == NULL) {
        root = (item*)malloc(sizeof(item));
        root->id = id;
        root->left_stock = left_stock;
        root->price = price;
        root->left = NULL;
        root->right = NULL;
    }
    else {
        if (id < root->id) {
            root->left = insert(root->left, id, left_stock, price);
        }
        else {
            root->right = insert(root->right, id, left_stock, price);
        }
    }
    return root;
}


item* stockSearch(item* root, int key) {
    while (root != NULL && root->id != key) {
        if (root->id > key) {
            root = root->left;
        }
        else {
            root = root->right;
        }
    }
    return root;
}

void stockTxt(item* root, FILE* fp) {
    if (root == NULL) return;
    stockTxt(root->left, fp);
    fprintf(fp, "%d %d %d\n", root->id, root->left_stock, root->price);
    stockTxt(root->right, fp);
    return;
}

void freeTree(item* root) {
    if (root == NULL) return;
    freeTree(root->left);
    freeTree(root->right);
    free(root);
    return;
}

void init_pool(int listenfd, pool* p) {
    /*Initially, there are no connected descriptors*/
    int i;
    p->maxi = -1;
    for (i = 0; i < FD_SETSIZE; i++)
        p->clientfd[i] = -1;

    /*Initially, listenfd is only member of select read set*/
    p->maxfd = listenfd;
    FD_ZERO(&p->read_set);
    FD_SET(listenfd, &p->read_set);
}

void add_client(int connfd, pool* p) {
    int i;
    p->nready--;
    for (i = 0; i < FD_SETSIZE; i++) {
        if (p->clientfd[i] < 0) {
            /*Add connected descriptor to the pool*/
            p->clientfd[i] = connfd;
            Rio_readinitb(&p->clientrio[i], connfd);

            /* Add the descriptor to descr iptor set */
            FD_SET(connfd, &p->read_set);
            /* Update max descriptor and pool high water mark */
            if (connfd > p->maxfd)
                p->maxfd = connfd;
            if (i > p->maxi)
                p->maxi = i;
            break;
        }
    }
    if (i == FD_SETSIZE) /* Couldn't find an empty slot */
        app_error("add_client error : Too many clients");
}

void check_clients(pool* p)
{
    int i, connfd, n;
    char buf[MAXLINE];
    rio_t rio;
    char cmd[10];
    int id, num;
    for (i = 0; (i <= p->maxi) && (p->nready > 0); i++) {
        connfd = p->clientfd[i];
        rio = p->clientrio[i];
        /* If the descriptor is ready, echo a text line from i t */
        if ((connfd > 0) && (FD_ISSET(connfd, &p->ready_set))) {
            p->nready--;
            if ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
                cmd[0] = '\0'; id = 0; num = 0;
                sscanf(buf, "%s %d %d", cmd, &id, &num);
                printf("server received %d bytes(%d)\n", n, connfd);
                if (!strcmp(cmd, "show")) {
                    for (int z = 0; z < MAXLINE; z++)str[z] = '\0';
                    show(connfd, root);
                    int slen = strlen(str);
                    str[slen++] = '\n';
                    str[slen] = '\0';
                    Rio_writen(connfd, str, MAXLINE);
                   //Rio_writen(connfd, "end\n", 4);
                }
                else if (!strcmp(cmd, "buy")) {
                    buy(connfd, root, id, num);
                }
                else if (!strcmp(cmd, "sell")) {
                    sell(connfd, root, id, num);
                }
                else {
                    Rio_writen(connfd, buf, MAXLINE);
                }
            }

            /* EOF detected, remove descriptor from pool */
            else {
                Close(connfd);
                FD_CLR(connfd, &p->read_set);
                p->clientfd[i] = -1;
                printf("disconnected %d\n", connfd);
            }
        }
    }
}

void show(int connfd, item* root) {
	char buf[MAXLINE];
	if (root != NULL) {
		sprintf(buf, "%d %d %d\n", root->id, root->left_stock, root->price);
        strcat(str, buf);
		show(connfd, root->left);
		show(connfd, root->right);
	}
}

void buy(int connfd, item* root, int id, int n) {
    item* stock = stockSearch(root, id);
    char buf[MAXLINE];
    if ((stock->left_stock - n) >= 0) {
        stock->left_stock -= n;
        strcpy(buf, "[buy] success\n\0");
        Rio_writen(connfd, buf, MAXLINE);
    }
    else {
        strcpy(buf, "Not enough left stock\n\0");
        Rio_writen(connfd, buf, MAXLINE);
    }
}

void sell(int connfd, item* root, int id, int n) {
	item* stock = stockSearch(root, id);
	char buf[MAXLINE];
	stock->left_stock += n;
    strcpy(buf, "[sell] success\n\0");
    Rio_writen(connfd, buf, MAXLINE);
}