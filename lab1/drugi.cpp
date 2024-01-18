#include <iostream>
#include <time.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <algorithm>
#include <random>
#include <unordered_map>

#define nl '\n'
#define NF 5
#define ZAH 50L
#define ODG 51L 

using namespace std;

class Message {

    public:
    int i;
    int t;

    Message(int i, int t): i(i), t(t) {}
    Message() : i(0), t(0) {}
};

ostream &operator<<(ostream &os, const Message &m) {
    os << '(' << m.i << ", " << m.t << ')';
    return os;
}

bool operator<(const Message &m1, const Message &m2) {
    if (m1.t == m2.t)
        return m1.i<m2.i;
    return m1.t < m2.t;
}
auto msgcomp = [](const Message &m1, const Message &m2) -> bool {
    return m1<m2;
};

int SHMID;
struct common_data_struct {
    bool forks[NF];
} *common;

int msgq[NF];
uid_t uid;
unordered_map<long, string> ss;

struct msg_t {
    long mtype;
    Message m;
};

void update_clock(int &i, int othertime) {
    i = max(i, othertime) + 1;
}










void clrmem() {
    //pobrisi zajednicku memoriju
    shmdt((char *) common);
    if (shmctl(SHMID, IPC_RMID, NULL) != -1)
        cout << "Izbrisana zajednicka memorija ID " << SHMID << nl;

    //pobrisi redove
    cout << "Brisem redove poruka..." << nl;
    for (int i=0; i<NF; i++) {
        if (msgctl(msgq[i], IPC_RMID, NULL) != -1)
            cout << "Izbrisan red poruka ID " << msgq[i] << nl;
    }
}

void mkmem() {
    ss[ZAH] = "ZAHTJEV";
    ss[ODG] = "ODGOVOR";



    SHMID = shmget(IPC_PRIVATE, sizeof(common_data_struct), 0600);
    if (SHMID == -1) {
        clrmem();
        exit(1);
    }
    common = (common_data_struct *) shmat(SHMID, NULL, 0);
    cout << "Uspjesno stvorena zajednicka memorija ID: " << SHMID << nl;
    for (int i=0; i<NF; i++) {
        common->forks[i] = true;
    }

    uid = getuid();
    for (int i=0; i<NF; i++) {
        if ((msgq[i] = msgget(uid+i+1, 0600 | IPC_CREAT)) == -1) {
            clrmem();
            exit(1);
        }
        cout << "Uspjesno stvoren red poruka za filozofa " << i 
                << ". ID: " << msgq[i] << nl;
    }

}

void interrupted(int signal) {
    cout << nl << "Izazvan je prekid signalom " << signal << endl;
    clrmem();
    exit(1);
}

void spavaj(timespec *tim, int lowms, int upms) {
    long sleeptimems = (rand() % (upms-lowms+1)) + lowms;
    tim->tv_sec = sleeptimems/1000L;
    sleeptimems /= 1000L;
    tim->tv_nsec = sleeptimems * 1000000L;
    nanosleep(tim, tim);
}

void log_msg_rcv(int f, const msg_t *msg, int current_clock) {
    
    cout << "Filozof " << f << ": od filozofa " << msg->m.i << 
                    " primio poruku " << ss[msg->mtype] << msg->m << "\t\t\t" <<
                    "(C" << f << " = " << current_clock << ") *" << nl;
                    
}

void log_msg_snd(int f, int to, const msg_t *msg, int current_clock) {
    
    cout << "Filozof " << f << ": filozofu " << to << 
                    " poslao poruku " << ss[msg->mtype] << msg->m << "\t\t\t" <<
                    "(C" << f << " = " << current_clock << ')' << nl;
}

void filozof(int f) {
    long dummy_msg;
    msgrcv(msgq[f], &dummy_msg, 0, 0, 0);
    cout << "Pocetak izvrsavanja procesa filozofa " << f << nl;
    unsigned seed = time(NULL) * (f+1);
    srand(seed);
    default_random_engine rng;
    rng.seed(seed);
    
    int leftfork = (f+1)%NF;
    int rightfork = f;
    int lf = (f+1)%NF;
    int rf = (f-1+NF)%NF;

    vector<int> others {lf, rf};
    timespec tim;
    int clock = 1;
    const auto siz = sizeof(Message);

    while(true) {
        spavaj(&tim, 1000, 5000); //misli

        //prvo "odgovori" na sve poruke iz svog reda
        msg_t msgbuf;
        msg_t odg;
        while (msgrcv(msgq[f], &msgbuf, siz, 0, IPC_NOWAIT) != -1) {
            update_clock(clock, msgbuf.m.t);
            log_msg_rcv(f, &msgbuf, clock);
            
            odg.mtype = ODG;
            odg.m.i = f;
            odg.m.t = msgbuf.m.t;
            msgsnd(msgq[msgbuf.m.i], &odg, siz, 0);
            log_msg_snd(f, msgbuf.m.i, &odg, clock);
        }

        //generiraj zahtjev za ulazak u KO
        msg_t zahmsg;
        zahmsg.mtype = ZAH;
        zahmsg.m.i = f;
        zahmsg.m.t = clock;
        
        shuffle(others.begin(), others.end(), rng); //simulacija kome prvome slati
        for (int &o : others) {
            msgsnd(msgq[o], &zahmsg, siz, 0);
            log_msg_snd(f, o, &zahmsg, clock);
        } //poslan zahtjev, sad cekaj sta dodje na redu poruka

        int others_answers = 0;
        unordered_map<int, int> unanswered({
            {lf, 0}, {rf, 0}
        });
        while(others_answers < 2) {
            msgrcv(msgq[f], &msgbuf, siz, 0, 0);
            update_clock(clock, msgbuf.m.t);
            log_msg_rcv(f, &msgbuf, clock);
            
            if (msgbuf.mtype == ZAH) {
                if (msgbuf.m < zahmsg.m) {
                    // primljen zahtjev je poslan prije
                    // posalji odgovor
                    odg.mtype = ODG;
                    odg.m.i = f;
                    odg.m.t = msgbuf.m.t;
                    msgsnd(msgq[msgbuf.m.i], &odg, siz, 0);
                    log_msg_snd(f, msgbuf.m.i, &odg, clock);
                    unanswered[msgbuf.m.i] = -2;

                } else {
                    unanswered[msgbuf.m.i] = msgbuf.m.t;
                }
            } else {
                // dobio odgovor
                others_answers++;
            }

        }

        // kriticni odsjecak
        cout << "Filozof " << f << " uzima  štapiće " << leftfork << rightfork << " i jede" << nl;
        bool both_available = common->forks[leftfork] && common->forks[rightfork];
        if (!both_available) {
            cout << "Filozof " << f << " nema dostupna oba štapića! Greška!" << nl;
            clrmem();
            exit(1);
        }
        common->forks[leftfork] = false;
        common->forks[rightfork] = false;
        spavaj(&tim, 1000, 5000);
        cout << "Filozof " << f << " spušta štapiće " << leftfork << rightfork << " i odlazi misliti" << nl;
        common->forks[rightfork] = true;
        common->forks[leftfork] = true;
        // izlaz iz KO
        for (auto &pair : unanswered) {
            if (pair.second > 0) {
                // posalji odgovor jer ceka na njega
                odg.mtype = ODG;
                odg.m.i = f;
                odg.m.t = pair.second;
                msgsnd(msgq[pair.first], &odg, siz, 0);
                log_msg_snd(f, pair.first, &odg, clock);
            }
        }

        
    }

    cout << "Zavrsetak izvrsavanja procesa filozofa " << f << nl;
}


int main() {
    cout << "Pocetak glavnog procesa" << nl;
    srand(time(NULL));
    sigset(SIGINT, interrupted);

    mkmem();

    int f;
	for (f = 0 ; f < NF; f++) {
        pid_t pid = fork();
		switch(pid) {
			case 0:
				filozof(f);
				exit(0);
			case -1:
				cout << "Greska pri stvaranju novog procesa!" << nl;
                clrmem();
				exit(1);
            default:
                cout << "Stvoren proces filozof broj " << f << ". PID: " << pid << nl;
			
		}
	}

    long dummy_msg;
    for (int i=0; i<NF; i++) {
        msgsnd(msgq[i], &dummy_msg, 0, 0);
    }

    for ( ; f > 0 ; f--)
		wait(NULL);

    clrmem();
    cout << "Normalan zavrsetak glavnog procesa" << endl;
    return 0;
}