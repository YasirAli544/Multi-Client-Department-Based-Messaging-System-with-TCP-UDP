/* client.c 
   - TCP connect + authentication
   - UDP heartbeat
   - Message routing (menu driven)
   - Clean readable UI
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define S_IP "127.0.0.1"
#define S_TCP 9000
#define S_UDP 9001
#define BUF 2048

void upcase(char *s){ for(;*s; ++s) *s = toupper((unsigned char)*s); }

static void strip(char *s){
    s[strcspn(s,"\n")] = 0;
}

int main() {
    int tcpFd=-1, udpFd=-1;
    struct sockaddr_in srvTcp, srvUdp, myUdp;
    char campus[64]={0}, dept[64]={0}, pass[128]={0};
    int myUdpPort=0;
    int authed = 0;

    printf("Client starting...\n");

    tcpFd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcpFd<0){ perror("tcp socket"); return 1; }

    memset(&srvTcp,0,sizeof(srvTcp));
    srvTcp.sin_family = AF_INET;
    srvTcp.sin_port = htons(S_TCP);
    inet_pton(AF_INET, S_IP, &srvTcp.sin_addr);

    if (connect(tcpFd, (struct sockaddr*)&srvTcp, sizeof(srvTcp)) < 0) {
        perror("connect");
        close(tcpFd);
        return 1;
    }

    udpFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpFd<0){ perror("udp"); close(tcpFd); return 1; }

    memset(&myUdp,0,sizeof(myUdp));
    myUdp.sin_family = AF_INET;
    myUdp.sin_addr.s_addr = INADDR_ANY;
    myUdp.sin_port = 0;
    if (bind(udpFd, (struct sockaddr*)&myUdp, sizeof(myUdp))<0) {
        perror("bind udp");
        close(tcpFd); close(udpFd);
        return 1;
    }
    socklen_t al=sizeof(myUdp);
    getsockname(udpFd, (struct sockaddr*)&myUdp, &al);
    myUdpPort = ntohs(myUdp.sin_port);

    memset(&srvUdp,0,sizeof(srvUdp));
    srvUdp.sin_family = AF_INET;
    srvUdp.sin_port = htons(S_UDP);
    inet_pton(AF_INET, S_IP, &srvUdp.sin_addr);

    /* ===========================
       LOGIN UI
       =========================== */
    printf("\n=============================\n");
    printf("        CLIENT LOGIN\n");
    printf("=============================\n");

    printf("Campus: ");
    fgets(campus, sizeof(campus), stdin); strip(campus);

    printf("Department: ");
    fgets(dept, sizeof(dept), stdin); strip(dept);

    printf("Password: ");
    fgets(pass, sizeof(pass), stdin); strip(pass);

    upcase(campus); upcase(dept);

    printf("\nConnecting...\n");

    /* send initial auth */
    char authBuf[BUF];
    snprintf(authBuf, sizeof(authBuf), "CAMPUS:%s;DEPT:%s;PASS:%s",
             campus, dept, pass);
    send(tcpFd, authBuf, strlen(authBuf), 0);

    time_t lastHB = 0;
    fd_set rfds;
    int maxfd = (tcpFd > udpFd) ? tcpFd : udpFd;

    while (1) {
        FD_ZERO(&rfds);
        FD_SET(tcpFd,&rfds);
        FD_SET(udpFd,&rfds);
        FD_SET(STDIN_FILENO,&rfds);

        struct timeval tv; tv.tv_sec=1; tv.tv_usec=0;
        int r = select(maxfd+1, &rfds, NULL,NULL,&tv);
        if (r < 0) { perror("select"); break; }

        /* heartbeat */
        if (authed) {
            time_t now = time(NULL);
            if (difftime(now, lastHB) >= 7) {
                char hb[256];
                snprintf(hb, sizeof(hb),
                        "HEARTBEAT;CAMPUS:%s;DEPT:%s;UDPPORT:%d",
                        campus, dept, myUdpPort);
                sendto(udpFd, hb, strlen(hb),0,
                       (struct sockaddr*)&srvUdp,sizeof(srvUdp));
                lastHB = now;
            }
        }

        /* TCP incoming */
        if (FD_ISSET(tcpFd,&rfds)) {
            char buf[BUF];
            int n = recv(tcpFd, buf, sizeof(buf)-1,0);
            if (n<=0){
                printf("Server disconnected.\n");
                break;
            }
            buf[n]=0;

            if (!authed){
                if (strncmp(buf,"AUTH_OK",7)==0){
                    authed = 1;

                    printf("\n====================================\n");
                    printf(" Logged in as: %s - %s\n", campus, dept);
                    printf("====================================\n");

                } else if (strncmp(buf,"WRONG_PASS",10)==0){
                    printf("Wrong password. Retry: ");
                    fgets(pass,sizeof(pass),stdin); strip(pass);

                    snprintf(authBuf,sizeof(authBuf),
                             "CAMPUS:%s;DEPT:%s;PASS:%s",
                             campus,dept,pass);
                    send(tcpFd, authBuf, strlen(authBuf),0);

                } else {
                    printf("Server: %s\n", buf);
                }
            }
            else {
                printf("\n[Message] %s\n", buf);
            }
        }

        /* UDP admin broadcast */
        if (FD_ISSET(udpFd,&rfds)) {
            char ub[BUF];
            struct sockaddr_in fr; socklen_t fl=sizeof(fr);
            int n = recvfrom(udpFd, ub, sizeof(ub)-1,0,
                             (struct sockaddr*)&fr,&fl);
            if (n>0){
                ub[n]=0;
                printf("\n[Admin Broadcast] %s\n", ub);
            }
        }

        /* ===========================
           MENU-DRIVEN USER INPUT
           =========================== */
        if (FD_ISSET(STDIN_FILENO,&rfds)) {

            if (!authed) {
                printf("Still not authenticated.\n");
                continue;
            }

            printf("\n=========================\n");
            printf("         MENU\n");
            printf("=========================\n");
            printf("1) Send Message\n");
            printf("2) Exit\n");
            printf("-------------------------\n");
            printf("Choice: ");

            char choice[16];
            if (!fgets(choice,sizeof(choice),stdin)) break;
            strip(choice);

            if (strcmp(choice,"2")==0) {
                printf("Exiting...\n");
                break;
            }

            if (strcmp(choice,"1")==0) {

                char tCampus[64], tDept[64], tMsg[1024];

                printf("\nTarget Campus: ");
                fgets(tCampus,sizeof(tCampus),stdin); strip(tCampus);

                printf("Target Department: ");
                fgets(tDept,sizeof(tDept),stdin); strip(tDept);

                printf("Message: ");
                fgets(tMsg,sizeof(tMsg),stdin); strip(tMsg);

                /* build routed message */
                char final[2048];
                snprintf(final,sizeof(final),
                         "%s-%s:%s",
                         tCampus, tDept, tMsg);

                send(tcpFd, final, strlen(final), 0);

                printf("Message sent.\n");
            }
            else {
                printf("Invalid choice.\n");
            }
        }
    }

    close(tcpFd);
    close(udpFd);
    return 0;
}
