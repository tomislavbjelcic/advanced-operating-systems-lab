#define _XOPEN_SOURCE
#define _XOPEN_SOURCE_EXTENDED

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>

#define POSJETITELJA 8
#define MAX_MJESTA 4
#define VOZNJI 3

#define ZNACKA 50L
#define SJEDNI 51L
#define SJEO 52L
#define USTANI 53L
#define USTAO 54L
#define DUMMY 88L

uid_t UID;
int MSGQ_ID_POS[POSJETITELJA];
int MSGQ_ID_VRT;

int SHMID;
struct common_data_struct {
    int present[POSJETITELJA];
    pid_t pids[POSJETITELJA];
};
struct common_data_struct *COMMON;

typedef struct znacka_msg_t {
    long mtype;
    char mtext[MAX_MJESTA+1];
} znacka_msg_t;
void znacka_init(znacka_msg_t *z) {
    z->mtype = ZNACKA;
    z->mtext[0] = 0;
}

typedef struct sjedni_msg_t {
    long mtype;
    char mtext[1];
} sjedni_msg_t;

typedef struct sjeo_msg_ack {
    long mtype;
    char mtext[1];
} sjeo_msg_ack;

typedef struct ustani_msg_t {
    long mtype;
    char mtext[1];
} ustani_msg_t;

typedef struct ustao_msg_ack {
    long mtype;
    char mtext[1];
} ustao_msg_ack;








void queue_remove(void) {
    //pobrisi zajednicku memoriju
    shmdt((char *)COMMON);
    if (shmctl(SHMID, IPC_RMID, NULL) != -1)
        printf("Izbrisana zajednicka memorija ID %d\n", SHMID);

    //pobrisi redove
    printf("Brisem redove poruka...\n");
    for (int i=0; i<POSJETITELJA; i++) {
        if (msgctl(MSGQ_ID_POS[i], IPC_RMID, NULL) != -1)
            printf("Izbrisan red poruka ID %d\n", MSGQ_ID_POS[i]);
    }
    if (msgctl(MSGQ_ID_VRT, IPC_RMID, NULL) != -1)
        printf("Izbrisan red poruka ID %d\n", MSGQ_ID_VRT);
}

void queue_create(void) {
    SHMID = shmget(IPC_PRIVATE, sizeof(struct common_data_struct), 0600);
    if (SHMID == -1) {
        queue_remove();
        exit(1);
    }
    printf("Uspješno stvorena zajednička memorija ID %d\n", SHMID);
    COMMON = shmat(SHMID, NULL, 0);

    UID = getuid();
    if ((MSGQ_ID_VRT = msgget(UID, 0600 | IPC_CREAT)) == -1) {
        queue_remove();
        exit(1);
    }
    printf("Uspješno stvoren red poruka vrtuljka ID %d\n", MSGQ_ID_VRT);
    for (int i=0; i<POSJETITELJA; i++) {
        if ((MSGQ_ID_POS[i] = msgget(UID+i+1, 0600 | IPC_CREAT)) == -1) {
            queue_remove();
            exit(1);
        }
        printf("Uspješno stvoren red poruka posjetitelja %d ID %d\n", i, MSGQ_ID_POS[i]);
    }
}

void interrupted(int signal) {
    printf("Prekid!!!\n");
    queue_remove();
    exit(1);
}

int get_next(int *arr, int N, int idx) {
    int nxt = -1;
    int first = (idx+1)%N;
    for (int i=0; i<N-1; i++) {
        int curridx = (first+i)%N;
        if (arr[curridx]) {
            nxt = curridx;
            break;
        }
    }
    return nxt;
}

typedef struct dummy_msg {
    long mtype;
    char mtext[1];
} dummy_msg;

void posjetitelj(int i) {
    dummy_msg dummy;
    msgrcv(MSGQ_ID_VRT, &dummy, 1, DUMMY, 0);
    printf("Početak izvršavanja procesa posjetitelja broj %d\n", i);
    srand(time(NULL) + i*5);
    
    struct timespec t;
    znacka_msg_t znbuf;
    sjedni_msg_t sjbuf;
    sjeo_msg_ack sjeo;
    ustao_msg_ack ustao;
    sjeo.mtype = SJEO;
    ustao.mtype = USTAO;
    ustani_msg_t usbuf;
    for (int rides=0; rides<VOZNJI; rides++) {
        t.tv_sec = 0;
        t.tv_nsec = rand()%1900 + 100;
        sigrelse(SIGCONT);
        while (nanosleep(&t, &t) == -1) {
            //proslijedi znacku
            int r = msgrcv(MSGQ_ID_POS[i], &znbuf, MAX_MJESTA+1, ZNACKA, IPC_NOWAIT);
            if (r == -1)
                continue;
            int next = get_next(COMMON->present, POSJETITELJA, i);
            if (msgsnd(MSGQ_ID_POS[next], &znbuf, MAX_MJESTA+1, 0) == -1) {
                perror("msgsnd");
                queue_remove();
                exit(1);
            }
            kill(COMMON->pids[next], SIGCONT);
        }

        //cekaj znacku
        //printf("Posjetitelj %d ceka na znacku\n", i);
        if (msgrcv(MSGQ_ID_POS[i], &znbuf, MAX_MJESTA+1, ZNACKA, 0) == -1) {
            if (errno == EINTR) {
                printf("Proces posjetitelj %d je potjeran doma jer nema dovoljno ostalih posjetitelja (ostalo mu je jos %d voznji.)\n"
                , i, VOZNJI-rides);
                break;
            }
            perror("msgrcv");
            queue_remove();
            exit(1);
        }
        sighold(SIGCONT);

        //dobio si je
        //printf("Posjetitelj %d dobio znacku\n", i);
        int filled = znbuf.mtext[0];
        znbuf.mtext[filled+1] = i;
        znbuf.mtext[0]++;
        if (filled == MAX_MJESTA-1) {
            // posalji poruku vrtuljku
            printf("Bukirana 4 mjesta: ");
            for (int p=0; p<MAX_MJESTA; p++)
                printf("%d ", znbuf.mtext[p+1]);
            printf("\n");
            if (msgsnd(MSGQ_ID_VRT, &znbuf, MAX_MJESTA+1, 0) == -1) {
                perror("msgsnd");
                queue_remove();
                exit(1);
            }
        } else {
            // proslijedi sljedećem
            int next = get_next(COMMON->present, POSJETITELJA, i);
            if (msgsnd(MSGQ_ID_POS[next], &znbuf, MAX_MJESTA+1, 0) == -1) {
                perror("msgsnd");
                queue_remove();
                exit(1);
            }
            kill(COMMON->pids[next], SIGCONT);
        }
        // u svakom slucaju cekaj vrtuljak
        msgrcv(MSGQ_ID_VRT, &sjbuf, 1, SJEDNI, 0);
        printf("Sjeo posjetitelj %d po %d. put.\n", i, rides+1);
        msgsnd(MSGQ_ID_VRT, &sjeo, 1, 0);
        msgrcv(MSGQ_ID_VRT, &usbuf, 1, USTANI, 0);
        printf("Sišao posjetitelj %d\n", i);
        msgsnd(MSGQ_ID_VRT, &ustao, 1, 0);

    }




    printf("Završetak izvršavanja procesa posjetitelja broj %d\n", i);
}

int main(void) {
    printf("Početak izvršavanja procesa vrtuljka\n");
    srand(time(NULL));
    sigset(SIGINT, interrupted); 

    queue_create();

    int i;
	for (i = 0 ; i < POSJETITELJA; i++) {
        int pid = fork();
		switch(pid) {
			case 0:
				posjetitelj(i);
				exit(0);
			case -1:
				printf("Greska pri stvaranju novog procesa!\n");
                queue_remove();
				exit(1);
            default:
                printf("Stvoren posjetitelj broj %d. PID=%d\n", i, pid);
                COMMON->pids[i]=pid;
                COMMON->present[i] = VOZNJI;
			
		}
	}
    dummy_msg dummy;
    dummy.mtype = DUMMY;
    for (int k=0; k<POSJETITELJA; k++)
        msgsnd(MSGQ_ID_VRT, &dummy, 1, 0);
    //VRTULJAK
    znacka_msg_t zn;
    sjedni_msg_t sj;
    ustani_msg_t us;
    sjeo_msg_ack sjeo;
    ustao_msg_ack ustao;
    sj.mtype = SJEDNI;
    us.mtype = USTANI;
    int active = POSJETITELJA;
    int last_from = POSJETITELJA;
    while (active > 0) {
        if (active < MAX_MJESTA) {
            // nema dovoljno posjetitelja za popuniti, potjeraj ih doma
            for (int p=0; p<POSJETITELJA; p++) {
                if (COMMON->present[p])
                    kill(SIGCONT, COMMON->pids[p]);
            }
            i -= active;
            break;
        }
        znacka_init(&zn);
        int to = get_next(COMMON->present, POSJETITELJA, last_from);
        // stavi znacku nazad u opticaj
        if (msgsnd(MSGQ_ID_POS[to], &zn, MAX_MJESTA+1, 0) == -1) {
            perror("msgsnd");
            queue_remove();
            exit(1);
        }

        msgrcv(MSGQ_ID_VRT, &zn, MAX_MJESTA+1, ZNACKA, 0);
        for (int m=0; m<MAX_MJESTA; m++) {
            int p = zn.mtext[m+1];
            COMMON->present[p]--;
            if (COMMON->present[p] == 0)
                active--;
        }
        last_from = zn.mtext[MAX_MJESTA];

        for (int m=0; m<MAX_MJESTA; m++) {
            msgsnd(MSGQ_ID_VRT, &sj, 1, 0);
        }
        for (int m=0; m<MAX_MJESTA; m++) {
            msgrcv(MSGQ_ID_VRT, &sjeo, 1, SJEO, 0);
        }
        printf("Pokrenuo vrtuljak\n");
        usleep(1000 * (rand()%2000+1000));

        for (int m=0; m<MAX_MJESTA; m++) {
            msgsnd(MSGQ_ID_VRT, &us, 1, 0);
        }
        for (int m=0; m<MAX_MJESTA; m++) {
            msgrcv(MSGQ_ID_VRT, &ustao, 1, USTAO, 0);
        }
    }
    

	
	for ( ; i > 0 ; i--)
		wait(NULL);

    queue_remove();
    printf("Završetak izvršavanja procesa vrtuljka.\n");
    return 0;
}