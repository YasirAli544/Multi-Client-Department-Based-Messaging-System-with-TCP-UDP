/* server.c
   Student-ish server 
   - TCP auth + routing
   - UDP heartbeats (clients)
   - UDP admin commands (LIST, BROADCAST)
   Protocols:
     Auth:   CAMPUS:<x>;DEPT:<y>;PASS:<p>
     HB:     HEARTBEAT;CAMPUS:<x>;DEPT:<y>;UDPPORT:<n>
     Admin:  ADMIN:LIST
             ADMIN:BROADCAST:<msg>
     Route:  TARGETCAMPUS-TARGETDEPT:message
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define TCP_PORT 9000
#define UDP_PORT 9001
#define MAX_CLIENTS 40
#define BUF 2048
#define HEART_STALE 60

/* small password table (Password System A) */
struct Pass { char campus[32]; char dept[32]; char pass[64]; };
struct Pass passTable[] = {
    {"LAHORE","CS","LHR_CS_123"},
    {"LAHORE","ADMIN","LHR_ADM_123"},
    {"CHINIOT","CS","CH_CS_123"},
    {"KARACHI","CS","KHI_CS_123"},
    {"ISLAMABAD","CS","ISB_CS_123"},
    {"MULTAN","ADMISSIONS","MTN_ADM_123"}
};
int passCount = sizeof(passTable)/sizeof(passTable[0]);

typedef struct {
    int tcpFd;
    char campus[48];
    char dept[48];
    int authed;          /* 0/1 */
    struct sockaddr_in udpAddr;
    int udpKnown;        /* 0/1 */
    time_t lastHeart;
} Client;

Client clients[MAX_CLIENTS];

void upcase(char *s) { for (; *s; ++s) *s = toupper((unsigned char)*s); }

void initClients() {
    for (int i=0;i<MAX_CLIENTS;i++){
        clients[i].tcpFd = -1;
        clients[i].campus[0]=0;
        clients[i].dept[0]=0;
        clients[i].authed = 0;
        clients[i].udpKnown = 0;
        clients[i].lastHeart = 0;
    }
}

int findFreeSlot() {
    for (int i=0;i<MAX_CLIENTS;i++) if (clients[i].tcpFd == -1) return i;
    return -1;
}

int findByFd(int fd) {
    for (int i=0;i<MAX_CLIENTS;i++) if (clients[i].tcpFd == fd) return i;
    return -1;
}

int findByCampusDept(const char *c, const char *d) {
    for (int i=0;i<MAX_CLIENTS;i++) {
        if (clients[i].tcpFd != -1 && clients[i].authed) {
            if (strcmp(clients[i].campus, c)==0 && strcmp(clients[i].dept,d)==0) return i;
        }
    }
    return -1;
}

int checkPassword(const char *c, const char *d, const char *p) {
    for (int i=0;i<passCount;i++) {
        if (strcmp(passTable[i].campus,c)==0 &&
            strcmp(passTable[i].dept,d)==0 &&
            strcmp(passTable[i].pass,p)==0) return 1;
    }
    return 0;
}

/* attempt auth; client may retry if WRONG_PASS */
void handleAuth(int slot, char *buf) {
    char camp[48]={0}, dept[48]={0}, pass[128]={0};
    if (sscanf(buf, "CAMPUS:%47[^;];DEPT:%47[^;];PASS:%127s", camp, dept, pass) < 3) {
        /* try fallback parsing (some human formats) */
        char *t = strdup(buf);
        char *tk = strtok(t, ";");
        while (tk) {
            if (strncmp(tk, "CAMPUS:",7)==0) strncpy(camp, tk+7, sizeof(camp)-1);
            else if (strncmp(tk, "DEPT:",5)==0) strncpy(dept, tk+5, sizeof(dept)-1);
            else if (strncmp(tk, "PASS:",5)==0) strncpy(pass, tk+5, sizeof(pass)-1);
            tk = strtok(NULL, ";");
        }
        free(t);
    }
    if (!camp[0] || !dept[0] || !pass[0]) {
        send(clients[slot].tcpFd, "SERVER_ERR: bad auth\n", 21, 0);
        return;
    }
    upcase(camp); upcase(dept);
    if (checkPassword(camp, dept, pass)) {
        clients[slot].authed = 1;
        strncpy(clients[slot].campus, camp, sizeof(clients[slot].campus)-1);
        strncpy(clients[slot].dept, dept, sizeof(clients[slot].dept)-1);
        clients[slot].udpKnown = 0;
        clients[slot].lastHeart = 0;
        send(clients[slot].tcpFd, "AUTH_OK\n", 8, 0);
        printf("[AUTH] slot %d => %s-%s\n", slot, camp, dept);
    } else {
        send(clients[slot].tcpFd, "WRONG_PASS\n", 11, 0);
        printf("[AUTH] wrong pass slot %d\n", slot);
    }
}

/* route a message: TARGETCAMPUS-TARGETDEPT:body */
void handleRoute(int slot, char *buf) {
    char tc[48]={0}, td[48]={0}, body[1200]={0};
    if (sscanf(buf, "%47[^-]-%47[^:]:%1199[^\n]", tc, td, body) < 3) {
        send(clients[slot].tcpFd, "SERVER_ERR: bad msg\n", 20, 0);
        return;
    }
    upcase(tc); upcase(td);
    int dest = findByCampusDept(tc, td);
    if (dest == -1) {
        send(clients[slot].tcpFd, "SERVER_ERR: not connected\n", 26, 0);
        return;
    }
    send(clients[dest].tcpFd, body, strlen(body), 0);
    printf("[ROUTE] %s-%s -> %s-%s\n",
           clients[slot].campus, clients[slot].dept,
           clients[dest].campus, clients[dest].dept);
}

/* process heartbeat or admin UDP */
void handleUdp(int usock) {
    char buf[BUF];
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    int n = recvfrom(usock, buf, sizeof(buf)-1, 0, (struct sockaddr *)&from, &fl);
    if (n <= 0) return;
    buf[n]=0;

    if (strncmp(buf, "ADMIN:", 6) == 0) {
        char *cmd = buf + 6;
        if (strncmp(cmd, "LIST", 4) == 0) {
            char out[4096]; out[0]=0;
            time_t now = time(NULL);
            for (int i=0;i<MAX_CLIENTS;i++) {
                if (clients[i].authed) {
                    int ago = clients[i].lastHeart ? (int)difftime(now, clients[i].lastHeart) : -1;
                    char line[200];
                    snprintf(line, sizeof(line), "%s-%s last=%d udp=%d\n",
                             clients[i].campus, clients[i].dept, ago, clients[i].udpKnown);
                    strncat(out, line, sizeof(out)-strlen(out)-1);
                }
            }
            if (!out[0]) strncpy(out, "NO_AUTHENTICATED_CLIENTS\n", sizeof(out)-1);
            sendto(usock, out, strlen(out), 0, (struct sockaddr *)&from, fl);
            return;
        } else if (strncmp(cmd, "BROADCAST:", 10) == 0) {
            char *msg = cmd + 10;
            if (!msg || !*msg) {
                sendto(usock, "ADMIN_ERR: empty\n", 17, 0, (struct sockaddr *)&from, fl);
                return;
            }
            for (int i=0;i<MAX_CLIENTS;i++) {
                if (clients[i].udpKnown) {
                    sendto(usock, msg, strlen(msg), 0,
                           (struct sockaddr *)&clients[i].udpAddr, sizeof(clients[i].udpAddr));
                }
            }
            sendto(usock, "ADMIN_OK: sent\n", 14, 0, (struct sockaddr *)&from, fl);
            printf("[ADMIN] broadcast done\n");
            return;
        } else {
            sendto(usock, "ADMIN_ERR: unknown\n", 18, 0, (struct sockaddr *)&from, fl);
            return;
        }
    }

    /* heartbeat */
    if (strncmp(buf, "HEARTBEAT;", 10)==0) {
        /* parse roughly */
        char *dup = strdup(buf);
        char *tk = strtok(dup, ";");
        char camp[48]={0}, dept[48]={0}; int uport=0;
        while (tk) {
            if (strncmp(tk, "CAMPUS:",7)==0) strncpy(camp, tk+7, sizeof(camp)-1);
            else if (strncmp(tk, "DEPT:",5)==0) strncpy(dept, tk+5, sizeof(dept)-1);
            else if (strncmp(tk, "UDPPORT:",8)==0) uport = atoi(tk+8);
            tk = strtok(NULL, ";");
        }
        free(dup);
        if (!camp[0] || !dept[0] || uport<=0) {
            /* bad hb, ignore */
            return;
        }
        upcase(camp); upcase(dept);
        /* find client record by campus+dept */
        for (int i=0;i<MAX_CLIENTS;i++) {
            if (clients[i].authed && strcmp(clients[i].campus, camp)==0 &&
                strcmp(clients[i].dept, dept)==0) {
                clients[i].udpAddr.sin_family = AF_INET;
                clients[i].udpAddr.sin_addr = from.sin_addr;
                clients[i].udpAddr.sin_port = htons(uport);
                clients[i].udpKnown = 1;
                clients[i].lastHeart = time(NULL);
                printf("[HB] %s-%s at %s:%d (slot %d)\n", camp, dept,
                       inet_ntoa(clients[i].udpAddr.sin_addr), uport, i);
                return;
            }
        }
        printf("[HB] unknown %s-%s\n", camp, dept);
    } else {
        printf("[UDP] unknown data: %.20s...\n", buf);
    }
}

/* periodic cleanup of stale UDP info */
void pruneStale() {
    time_t now = time(NULL);
    for (int i=0;i<MAX_CLIENTS;i++) {
        if (clients[i].udpKnown && clients[i].lastHeart &&
            difftime(now, clients[i].lastHeart) > HEART_STALE) {
            clients[i].udpKnown = 0;
        }
    }
}

int main() {
    int listenFd, udpFd;
    struct sockaddr_in taddr, uaddr;

    initClients();

    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) { perror("tcp socket"); return 1; }
    int yes=1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(&taddr,0,sizeof taddr);
    taddr.sin_family = AF_INET;
    taddr.sin_addr.s_addr = INADDR_ANY;
    taddr.sin_port = htons(TCP_PORT);
    if (bind(listenFd, (struct sockaddr*)&taddr, sizeof(taddr))<0) { perror("bind"); close(listenFd); return 1; }
    if (listen(listenFd, 8) < 0) { perror("listen"); close(listenFd); return 1; }

    udpFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpFd < 0) { perror("udp socket"); close(listenFd); return 1; }
    memset(&uaddr,0,sizeof uaddr);
    uaddr.sin_family = AF_INET;
    uaddr.sin_addr.s_addr = INADDR_ANY;
    uaddr.sin_port = htons(UDP_PORT);
    if (bind(udpFd, (struct sockaddr *)&uaddr, sizeof(uaddr))<0) { perror("bind udp"); close(listenFd); close(udpFd); return 1; }

    printf("Server running TCP %d UDP %d\n", TCP_PORT, UDP_PORT);

    fd_set rfds;
    int maxfd;
    time_t lastPrint = time(NULL);

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(listenFd, &rfds);
        FD_SET(udpFd, &rfds);
        maxfd = (listenFd > udpFd) ? listenFd : udpFd;

        for (int i=0;i<MAX_CLIENTS;i++){
            if (clients[i].tcpFd != -1) {
                FD_SET(clients[i].tcpFd, &rfds);
                if (clients[i].tcpFd > maxfd) maxfd = clients[i].tcpFd;
            }
        }

        struct timeval tv; tv.tv_sec = 1; tv.tv_usec = 0;
        int r = select(maxfd+1, &rfds, NULL, NULL, &tv);
        if (r < 0) { perror("select"); break; }

        if (difftime(time(NULL), lastPrint) >= 10) {
            // light status print
            printf("=== status ===\n");
            for (int i=0;i<MAX_CLIENTS;i++) {
                if (clients[i].authed) {
                    printf("slot %d: %s-%s fd=%d udp=%d\n",
                           i, clients[i].campus, clients[i].dept, clients[i].tcpFd, clients[i].udpKnown);
                }
            }
            lastPrint = time(NULL);
            pruneStale();
        }

        /* new TCP connect */
        if (FD_ISSET(listenFd, &rfds)) {
            struct sockaddr_in ca; socklen_t cal = sizeof(ca);
            int cfd = accept(listenFd, (struct sockaddr*)&ca, &cal);
            if (cfd < 0) perror("accept");
            else {
                int slot = findFreeSlot();
                if (slot == -1) {
                    printf("max clients; reject\n");
                    close(cfd);
                } else {
                    clients[slot].tcpFd = cfd;
                    clients[slot].authed = 0;
                    clients[slot].udpKnown = 0;
                    clients[slot].lastHeart = 0;
                    clients[slot].campus[0]=0; clients[slot].dept[0]=0;
                    printf("new client fd=%d slot=%d\n", cfd, slot);
                }
            }
        }

        /* udp in */
        if (FD_ISSET(udpFd, &rfds)) {
            handleUdp(udpFd);
        }

        /* tcp clients */
        for (int i=0;i<MAX_CLIENTS;i++){
            int fd = clients[i].tcpFd;
            if (fd != -1 && FD_ISSET(fd, &rfds)) {
                char buf[BUF];
                int n = recv(fd, buf, sizeof(buf)-1, 0);
                if (n <= 0) {
                    printf("client disconnected slot %d\n", i);
                    close(fd);
                    clients[i].tcpFd = -1;
                    clients[i].authed = 0;
                    clients[i].udpKnown = 0;
                    clients[i].campus[0]=0; clients[i].dept[0]=0;
                    clients[i].lastHeart = 0;
                } else {
                    buf[n]=0;
                    if (!clients[i].authed) handleAuth(i, buf);
                    else handleRoute(i, buf);
                }
            }
        }
    }

    close(listenFd);
    close(udpFd);
    return 0;
}
