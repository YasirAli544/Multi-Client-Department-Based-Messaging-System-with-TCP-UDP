/* admin.c 
   - Simple UDP control panel
   - LIST + BROADCAST
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define SERV_IP "127.0.0.1"
#define SERV_UDP 9001
#define BUF 2048

static void readLine(char *b, int s){
    if (!fgets(b,s,stdin)) { b[0]=0; return; }
    b[strcspn(b,"\n")] = 0;
}

int main() {
    int s;
    struct sockaddr_in me, srv;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s<0){ perror("socket"); return 1; }

    memset(&me,0,sizeof(me));
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = INADDR_ANY;
    me.sin_port = 0;
    if (bind(s,(struct sockaddr*)&me,sizeof(me))<0) {
        perror("bind");
        close(s);
        return 1;
    }

    socklen_t ln=sizeof(me);
    getsockname(s,(struct sockaddr*)&me,&ln);
    int myPort = ntohs(me.sin_port);

    memset(&srv,0,sizeof(srv));
    srv.sin_family = AF_INET;
    srv.sin_port = htons(SERV_UDP);
    inet_pton(AF_INET, SERV_IP, &srv.sin_addr);

    printf("Admin tool started (local UDP %d)\n", myPort);

    while (1) {
        /* ===== minimal clean UI ===== */
        printf("\n=============================\n");
        printf("         ADMIN PANEL\n");
        printf("=============================\n");
        printf("1) Show active clients\n");
        printf("2) Broadcast message\n");
        printf("3) Exit\n");
        printf("-----------------------------\n");
        printf("Choice: ");

        char choice[16];
        readLine(choice, sizeof(choice));

        if (strcmp(choice,"1")==0) {
            sendto(s,"ADMIN:LIST",10,0,(struct sockaddr*)&srv,sizeof(srv));

            fd_set f; FD_ZERO(&f); FD_SET(s,&f);
            struct timeval tv={3,0};
            if (select(s+1,&f,NULL,NULL,&tv) > 0) {
                char buf[BUF];
                struct sockaddr_in fr; socklen_t fl=sizeof(fr);
                int n = recvfrom(s,buf,sizeof(buf)-1,0,
                                 (struct sockaddr*)&fr,&fl);
                if (n>0) {
                    buf[n]=0;
                    printf("\nActive Clients:\n");
                    printf("-----------------------------\n");
                    printf("Campus       Dept       HB    UDP\n");
                    printf("-----------------------------\n");
                    printf("%s", buf);
                    printf("-----------------------------\n");
                }
            } else {
                printf("No reply from server.\n");
            }
        }
        else if (strcmp(choice,"2")==0) {
            char msg[BUF];
            printf("\nBroadcast message:\n> ");
            readLine(msg,sizeof(msg));

            if (!msg[0]) {
                printf("Empty message ignored.\n");
                continue;
            }

            char out[4096];
            snprintf(out,sizeof(out),"ADMIN:BROADCAST:%s", msg);
            sendto(s,out,strlen(out),0,(struct sockaddr*)&srv,sizeof(srv));

            fd_set f; FD_ZERO(&f); FD_SET(s,&f);
            struct timeval tv={3,0};
            if (select(s+1,&f,NULL,NULL,&tv) > 0) {
                char buf[256];
                struct sockaddr_in fr; socklen_t fl=sizeof(fr);
                int n=recvfrom(s,buf,sizeof(buf)-1,0,
                               (struct sockaddr*)&fr,&fl);
                if (n>0) {
                    buf[n]=0;
                    printf("Server Response: %s\n",buf);
                }
            } else {
                printf("No ack from server.\n");
            }
        }
        else if (strcmp(choice,"3")==0) {
            printf("Exiting admin...\n");
            break;
        }
        else {
            printf("Invalid choice.\n");
        }
    }

    close(s);
    return 0;
}
