#include <cstdlib>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <netdb.h>
#include <string.h>
#include <thread>
#include <mutex>
#include <unordered_set>
#include <signal.h>
#include <vector>
#include <set>
#include <condition_variable>
#include <map>
#include <iostream>
#include <chrono> 
using namespace std::chrono; 


class Question{
public:
    std::string questionText;
    std::string answearA, answearB, answearC, answearD;
    std::string correctAnswear;
    int answearTime;
};

class Quiz{
public:
    std::vector<Question> questions;
    std::string quizTitle;
    Quiz(){

    }
    Quiz(std::vector<Question> question_set,std::string title){
        questions = question_set;
        quizTitle = title;
    }
    void addQuestion(Question q){
        questions.push_back(q);
    }
};

class Player{
private:
    std::string nickname;
    int playerID;
    int score = 0;
    bool waiting;

public:

    Player(int id){
        playerID = id;
        nickname = "player ";
        nickname += std::to_string(id);
    }
    Player(){
        nickname = "offline";
    }
    void addToScore(int amount)         {score += amount;}

    std::string getNickname()           {return nickname;}
    int getScore()                      {return score;}
    int getPlayerID()                   {return playerID;}
    bool getWaiting()                   {return waiting;}

    void setNickname(std::string nick)  {nickname = nick;}
    void setScore(int scr)              {score = scr;}
    void setPlayerID(int id)            {playerID = id;}
    void setWaiting(bool b)             {waiting = b;}
};


class Room{
public:
    Player owner;
    int RoomId;
    int playerCount = 0;
    int playerAnswearsCount = 0;
    bool inGame = false;
    std::unordered_set<int> playersInRoom;
    Quiz quiz;
    
    Room(Player ownr){
        owner = ownr;
        RoomId =owner.getPlayerID()*123;
    }

    ~Room(){
        //printf("Room destructor called\n");
        playersInRoom.clear();
    }

    void addPlayer(int playerID){
        playersInRoom.insert(playerID);
        playerCount ++;
    }

    void removePlayer(int playerID){
        playersInRoom.erase(playerID);
        playerCount --;
    }

    void addAnswear(){
        playerAnswearsCount++;
    }

};



// store player info
// TODO: use int -> Player map instead
Player players[100];

int playersConnected = 0;

std::mutex notifyFdMutex;
std::mutex notifyRoomMutex;

// server socket
int servFd;

std::condition_variable onlinePlayersCv;

std::condition_variable controlQuestionsCv;

std::condition_variable startGameCv;

std::condition_variable endGameCv;

std::condition_variable endRoundCv;

// determines which controlQuestionsCv to notify
int notifyFd = 0;
int notifyRoomId = 0;

// client sockets
std::mutex clientFdsLock;
std::unordered_set<int> clientFds;

// game rooms
std::map<int,Room> gameRooms;

std::vector<Quiz> quizSet;

// handles SIGINT
void ctrl_c(int);

// handles interaction with the client
void clientLoop(int clientFd, char * buffer);

// prompts client to provide a valid nickname
void setPlayerNickname(int fd);

// checks nickname availability
bool validNickname(std::string nickname);

// displays a nickname list of onilne players
void displayPlayers();

// sends given questions to the players 
void askQuestion(Question q,std::unordered_set<int> players,int ownerFd);

void questionHandler(Question q,std::unordered_set<int> players);

void answearHandler(Question q,std::unordered_set<int> players_set,int owner);

void questionTimer(int seconds);

void sendScoreBoard(std::unordered_set<int> playersInRoom); 

void createQuiz(int clientFd);

void loadSampleQuizzes();

void handleLeave(int clientFd);

bool isNumeric(std::string str);

// converts cstring to port
uint16_t readPort(char * txt);

// sets SO_REUSEADDR
void setReuseAddr(int sock);

int main(int argc, char ** argv){

    // get and validate port number
    if(argc != 2) error(1, 0, "Need 1 arg (port)");
    auto port = readPort(argv[1]);
    
    // create socket
    servFd = socket(AF_INET, SOCK_STREAM, 0);
    if(servFd == -1) error(1, errno, "socket failed");
    
    // graceful ctrl+c exit
    signal(SIGINT, ctrl_c);

    // prevent dead sockets from raising pipe signals on write
    signal(SIGPIPE, SIG_IGN);
    
    setReuseAddr(servFd);
    
    // bind to any address and port provided in arguments
    sockaddr_in serverAddr{.sin_family=AF_INET, .sin_port=htons((short)port), .sin_addr={INADDR_ANY}};
    int res = bind(servFd, (sockaddr*) &serverAddr, sizeof(serverAddr));
    if(res) error(1, errno, "bind failed");
    
    printf("Server started.\n");

    printf("Listening  for connections...\n");

    // enter listening mode
    res = listen(servFd, 1);
    if(res) error(1, errno, "listen failed");

    // load sample quizzes
    loadSampleQuizzes();
    

/****************************/

    
    while(true){
   
        // prepare placeholders for client address
        sockaddr_in clientAddr{};
        socklen_t clientAddrSize = sizeof(clientAddr);
        
        // accept new connection
        auto clientFd = accept(servFd, (sockaddr*) &clientAddr, &clientAddrSize);
        if(clientFd == -1) error(1, errno, "accept failed");
        
        // add client to all clients set
        {
            std::unique_lock<std::mutex> lock(clientFdsLock);
            clientFds.insert(clientFd);
        }
        
        // tell who has connected
        printf("new connection from: %s:%hu (fd: %d)\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFd);
        
        // create a new player
        Player p(clientFd);
        players[clientFd] = Player(clientFd);
        //playersConnected ++;

        displayPlayers();



// client threads 
/******************************/
        std::thread([clientFd]{
            char buffer[255];
            clientLoop(clientFd, buffer);
        
        }).detach();   
    }
/*****************************/
}

uint16_t readPort(char * txt){
    char * ptr;
    auto port = strtol(txt, &ptr, 10);
    if(*ptr!=0 || port<1 || (port>((1<<16)-1))) error(1,0,"illegal argument %s", txt);
    return port;
}

void setReuseAddr(int sock){
    const int one = 1;
    int res = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if(res) error(1,errno, "setsockopt failed");
}

void ctrl_c(int){
    std::unique_lock<std::mutex> lock(clientFdsLock);
    
    // TEST
    // Change condition variable so the question asking thread shuts down. 
    onlinePlayersCv.notify_one();
    controlQuestionsCv.notify_all();
    
    for(int clientFd : clientFds){
        const char* msg = "Server shut down!\n";
        if(send(clientFd, msg, strlen(msg)+1, MSG_DONTWAIT) != (int)strlen(msg) + 1){
            perror("Server down message error");
        }
        shutdown(clientFd, SHUT_RDWR);
        close(clientFd);
    }
    close(servFd);
    printf("Closing server\n");
    exit(0);
}


// Client server interface
void clientLoop(int clientFd, char * buffer){
    
    std::mutex m;

    printf("%s has connected to the server\n", players[clientFd].getNickname().c_str());
    setPlayerNickname(clientFd);
    
    while(true){
        char menuMsg[] = "=== \"kahoot\" menu ===\n1.Host a game.\n2.Join a room\n3.Exit\n";
        if(send(clientFd, menuMsg, strlen(menuMsg)+1, MSG_DONTWAIT) != (int)strlen(menuMsg) + 1){
        std::unique_lock<std::mutex> lock(clientFdsLock);
        perror("Send error (menu)");
        clientFds.erase(clientFd);
        break;
        }

        char buffer[255];
        memset(buffer,0,255);
        if(read(clientFd,buffer,255) < 0){
            perror("Read error (menu)");
            std::unique_lock<std::mutex> lock(clientFdsLock);
            clientFds.erase(clientFd);
            playersConnected --;
            break;
        }

        if(strcmp(buffer,"1\n") == 0){
            char menuMsg[] = "=== \"kahoot\" menu ===\n1.Choose a quiz.\n2.Create a quiz set\n3.Go back\n";
            if(send(clientFd, menuMsg, strlen(menuMsg)+1, MSG_DONTWAIT) != (int)strlen(menuMsg) + 1){
                std::unique_lock<std::mutex> lock(clientFdsLock);
                perror("Send error (menu)");
                clientFds.erase(clientFd);
                break;
            }
            if(read(clientFd,buffer,255) < 0){
                perror("Read error (menu)");
                std::unique_lock<std::mutex> lock(clientFdsLock);
                clientFds.erase(clientFd);
                playersConnected --;
                break;
            }
            if(strcmp(buffer,"1\n") == 0){
                // createRoom function
                Room r(players[clientFd]);
                char menuMsg[4096] = "Choose quiz set:\n";
                for (unsigned i=0; i<quizSet.size(); i++){
                    strcat(menuMsg,std::to_string(i+1).c_str());
                    strcat(menuMsg,". ");
                    strcat(menuMsg,quizSet.at(i).quizTitle.c_str());
                    strcat(menuMsg,"\n");
                    }

                memset(buffer,0,sizeof(buffer));
                do{
                    if(send(clientFd, menuMsg, strlen(menuMsg)+1, MSG_DONTWAIT) != (int)strlen(menuMsg) + 1){
                        std::unique_lock<std::mutex> lock(clientFdsLock);
                        perror("Send error (menu)");
                        clientFds.erase(clientFd);
                        break;
                    }
                    memset(buffer,0,sizeof(buffer));  
                    if(read(clientFd,buffer,255) < 0){
                        perror("Read error (menu)");
                        std::unique_lock<std::mutex> lock(clientFdsLock);
                        clientFds.erase(clientFd);
                        playersConnected --;
                        break;
                    }
                    printf("%d\n",atoi(buffer));
                    printf("%d\n",(int)(quizSet.size()));
                }while(atoi(buffer)-1 >= (int)(quizSet.size()) || atoi(buffer)-1 < 0);
                printf("Quiz picked: %s",quizSet.at(atoi(buffer)-1).quizTitle.c_str());

                r.quiz = quizSet.at(atoi(buffer)-1);   
                //gameRooms.insert(std::pair<int,Room>(r.RoomId,r));
                strcpy(menuMsg,"Successfully created a room. Room id:");
                strcat(menuMsg,std::to_string(r.RoomId).c_str());
                strcat(menuMsg,"\n1.Start the game\n2.Exit\n===Awaiting players===\n");
                if(send(clientFd, menuMsg, strlen(menuMsg)+1, MSG_DONTWAIT) != (int)strlen(menuMsg) + 1){
                    std::unique_lock<std::mutex> lock(clientFdsLock);
                    perror("Send error (menu)");
                    clientFds.erase(clientFd);
                    break;
                }
                strcpy(buffer,"\0");
                /*
                // test question
                Question testQuestion = Question();
                testQuestion.questionText = "A is the correct answear.";
                testQuestion.answearA = "Answear 1";
                testQuestion.answearB = "Answear 2";
                testQuestion.answearC = "Answear 3";
                testQuestion.answearD = "Answear 4";
                testQuestion.correctAnswear = "A";
                testQuestion.answearTime = 10;

                //test quiz
                Quiz testQuiz = Quiz();
                testQuiz.addQuestion(testQuestion);
                testQuestion.answearTime = 12;
                testQuiz.addQuestion(testQuestion);
                testQuestion.answearTime = 11;
                testQuiz.addQuestion(testQuestion);
                testQuiz.addQuestion(testQuestion);
                testQuiz.quizTitle = "Quiz title";

                r.quiz = testQuiz;
                */
                gameRooms.insert(std::pair<int,Room>(r.RoomId,r));

                if(read(clientFd,buffer,255) < 0){
                    perror("Read error (menu)");
                    std::unique_lock<std::mutex> lock(clientFdsLock);
                    clientFds.erase(clientFd);
                    playersConnected --;
                    break;
                }

                if(strcmp(buffer,"1\n") == 0){
                    //printf("Starting the game.\n");
                    notifyFd = 0;
                    notifyRoomId = 0;

                    // resets player score before the game
                    for(int playerFd : gameRooms.find(clientFd*123)->second.playersInRoom){
                        players[playerFd].setScore(0);
                    }

                    
                    //std::thread([testQuestion,clientFd,r]{
                    for(Question q : r.quiz.questions){
                        notifyRoomMutex.lock();
                        notifyRoomId = r.RoomId;
                        startGameCv.notify_all();
                        notifyRoomMutex.unlock();
                        gameRooms.find(clientFd*123)->second.inGame = true;
                        askQuestion(q,gameRooms.find(clientFd*123)->second.playersInRoom,clientFd); 
                        //printf("Waiting for the players to answear...\n");
                        //printf("BEFORE:\n");
                        //int seconds = q.answearTime;
                        //std::thread waitThread(questionTimer,seconds);
                        //waitThread.join();
                        std::mutex m4;
                        std::unique_lock<std::mutex> ul4(m4);
                        controlQuestionsCv.wait(ul4,[clientFd] {
                             return ( gameRooms.find(123*clientFd)->second.playerAnswearsCount == gameRooms.find(123*clientFd)->second.playerCount || notifyFd == -1) ? true : false;
                            });
                        
                        //printf("AFTER:\n");
                        //printf("PlayersAnswears:\n",);
                        //printf("Ending question asking thread client:%d\n",clientFd);
                    }

                    // send score board to the players
                    sendScoreBoard(gameRooms.find(clientFd*123)->second.playersInRoom);

                    notifyRoomMutex.lock();
                    notifyFdMutex.lock();
                    notifyFd = 0;
                    notifyRoomId = 0;gameRooms.find(clientFd*123)->second.playersInRoom,
                    gameRooms.find(clientFd*123)->second.inGame = false;
                    startGameCv.notify_all();
                    notifyFdMutex.unlock();
                    notifyRoomMutex.unlock();
                    endGameCv.notify_all();
                    printf("Closing game room ...\n");
                    
                    //This is not a good solution 
                    sleep(1);
                    gameRooms.erase(r.RoomId);
                    strcpy(buffer,"\0");
                    continue;
                }  

                if(strcmp(buffer,"2\n") == 0){
                    printf("Closing game room ...\n");
                    notifyRoomId = r.RoomId;
                    gameRooms.find(clientFd*123)->second.inGame = false;
                    startGameCv.notify_all();
                    endGameCv.notify_all();

                    gameRooms.erase(r.RoomId);
                    strcpy(buffer,"\0");
                    continue;
                }  
            }

            if(strcmp(buffer,"2\n") == 0){
                std::cout << "Quiz list:\n";
                
                for (std::vector<Quiz>::iterator it=quizSet.begin(); it!=quizSet.end(); ++it){
                    printf("%s\n",it->quizTitle.c_str());
                }
                createQuiz(clientFd);
                // browse through quizzes
                strcpy(buffer,"\0");
                continue;
            }
            if(strcmp(buffer,"3\n") == 0){
                strcpy(buffer,"\0");
                continue;
            }  
        
        }

        if(strcmp(buffer,"2\n") == 0){
            char menuMsg[] = "=== \"kahoot\" menu ===\nPass in lobby id (type 3 to exit):";
            if(send(clientFd, menuMsg, strlen(menuMsg)+1, MSG_DONTWAIT) != (int)strlen(menuMsg) + 1){
                std::unique_lock<std::mutex> lock(clientFdsLock);
                perror("Send error (menu)");
                clientFds.erase(clientFd);
                break;
            }
            if(read(clientFd,buffer,255) < 0){
                perror("Read error (menu)");
                std::unique_lock<std::mutex> lock(clientFdsLock);
                clientFds.erase(clientFd);
                playersConnected --;
                break;
            }
            bool roomExists = false;
            Room* currentRoom;
            for (std::map<int,Room>::iterator it=gameRooms.begin(); it!=gameRooms.end(); ++it){
                if(atoi(buffer) == it->first && it->second.inGame == false){
                    currentRoom = &it->second;
                    roomExists = true;
                    it->second.addPlayer(clientFd);
                    char menuMsg[255] = "Player ";
                    strcat(menuMsg,players[clientFd].getNickname().c_str());
                    strcat(menuMsg," has joined your room !\n");
                    if(send(it->second.owner.getPlayerID(), menuMsg, strlen(menuMsg)+1, MSG_DONTWAIT) != (int)strlen(menuMsg) + 1){
                        std::unique_lock<std::mutex> lock(clientFdsLock);
                        perror("Send error (menu)");
                        clientFds.erase(it->second.owner.getPlayerID());
                        break;
                    }
                    break;
                }
            }
            if(!roomExists){
                char menuMsg[] = "Room does not exist.\n";
                if(send(clientFd, menuMsg, strlen(menuMsg)+1, MSG_DONTWAIT) != (int)strlen(menuMsg) + 1){
                    std::unique_lock<std::mutex> lock(clientFdsLock);
                    perror("Send error (menu)");
                    clientFds.erase(clientFd);
                    break;
                }
            }
            else{
            char menuMsg2[255] = "You have joined the room. Room id:";
            strcat(menuMsg2,std::to_string(currentRoom->RoomId).c_str());
            strcat(menuMsg2,"\nQuiz category :");
            strcat(menuMsg2,currentRoom->quiz.quizTitle.c_str());
            strcat(menuMsg2,"\nWaiting for the game to start.\n");
            if(send(clientFd, menuMsg2, strlen(menuMsg2)+1, MSG_DONTWAIT) != (int)strlen(menuMsg2) + 1){
                std::unique_lock<std::mutex> lock(clientFdsLock);
                perror("Send error (menu)");
                clientFds.erase(clientFd);
                break;
            }
            players[clientFd].setWaiting(true);

            std::thread leave(handleLeave,clientFd);

            //Wait for the host to start the game 
            std::mutex m2;
            std::unique_lock<std::mutex> lock1(m2);
            //printf("BEFORE:\n");
            //printf("Current room id: %d notifyRoomId: %d\n",currentRoom->RoomId,notifyRoomId);
            //printf("clientFd : %d notifyFd: %d\n",clientFd,notifyFd);
            startGameCv.wait(lock1,[currentRoom,clientFd] { return (currentRoom->RoomId == notifyRoomId || notifyFd == clientFd) ? true : false; });
            //printf("AFTER:\n");
            //printf("Current room id: %d notifyRoomId: %d\n",currentRoom->RoomId,notifyRoomId);
            //printf("clientFd : %d notifyFd: %d\n",clientFd,notifyFd);
            //printf("player %d has been notified\n",clientFd);

            // takes player back to main menu if they decide to leave
            if(players[clientFd].getWaiting() == false){
                printf("Player has left the room\n");
                char menuMsg[255] = "Player ";
                strcat(menuMsg,players[clientFd].getNickname().c_str());
                strcat(menuMsg," has left your room !\n");
                if(send(currentRoom->owner.getPlayerID(), menuMsg, strlen(menuMsg)+1, MSG_DONTWAIT) != (int)strlen(menuMsg) + 1){
                    std::unique_lock<std::mutex> lock(clientFdsLock);
                    perror("Send error (menu)");
                    clientFds.erase(currentRoom->owner.getPlayerID());
                    break;
                }
                currentRoom->removePlayer(clientFd);
                notifyFdMutex.lock();
                notifyFd = 0;
                notifyRoomId = 0;
                startGameCv.notify_all();
                notifyFdMutex.unlock();
                leave.join();
                continue;
            }
            players[clientFd].setWaiting(false);
            leave.join();
            
            printf("Game has started for player %d!\n",clientFd);
            /*
            if(read(clientFd,buffer,255) < 0){
                perror("Read error (menu)");
                std::unique_lock<std::mutex> lock(clientFdsLock);
                clientFds.erase(clientFd);
                playersConnected --;
                break;
            }
            */
            printf("Player %d waiting for endGame notification\n",clientFd);
            //std::unique_lock<std::mutex> ul(m);
            //endGameCv.wait(lock2,[currentRoom] { return (currentRoom->RoomId == notifyRoomId) ? true : false; });
            
            //controlQuestionsCv.wait(ul,[clientFd] { return (clientFd == notifyFd) ? true : false; });
            //printf("Round has ended!\n");
            std::mutex m3;
            std::unique_lock<std::mutex> ul2(m3);
            endGameCv.wait(ul2,[currentRoom] { return (currentRoom->inGame == false) ? true : false; });
            //printf("Game has ended!\n");
            strcpy(buffer,"\0");
            notifyFd = 0;
            }
        }

        if(strcmp(buffer,"3\n") == 0){
            break;
        }
    }
    shutdown(clientFd, SHUT_RDWR);
    close(clientFd);
    clientFds.erase(clientFd);
    printf("Ending service for client %d\n", clientFd);
}

// Handles nickname selection
// FINISHED
void setPlayerNickname(int clientFd){
    const char* msg1 =  "Choose your nickname:\n";
    if(send(clientFd, msg1, strlen(msg1)+1, MSG_DONTWAIT) != (int)strlen(msg1) + 1){
        std::unique_lock<std::mutex> lock(clientFdsLock);
        perror("Choose your nickname setup message failed");
        clientFds.erase(clientFd);
    }
    char buffer[64];
    decltype(clientFds) bad;
    while(true)
    {
        memset(buffer,0,sizeof(buffer));
        // TODO: deal with buffer overflow
        if (recv(clientFd,buffer,64,MSG_FASTOPEN) > 0){
            // Swapping '\n' for a null character

            buffer[strlen(buffer)-1] = '\0';
            int r = strlen(buffer);
            if(validNickname(buffer) && r <= 16 && r >= 3){
                players[clientFd].setNickname(buffer);
                const char* msg =  "Nickname set !\n";
                if(send(clientFd, msg, strlen(msg)+1, MSG_DONTWAIT) != (int)strlen(msg) + 1){
                    std::unique_lock<std::mutex> lock(clientFdsLock);
                    perror("Nickname set message failed");
                    clientFds.erase(clientFd);
                }
                playersConnected ++;
                onlinePlayersCv.notify_one();
                break;
            }
            else if(r < 3){
                const char* msg = "Nickname too short ! Try something with at least 3 characters:\n";
                if(send(clientFd, msg, strlen(msg)+1, MSG_DONTWAIT) != (int)strlen(msg) + 1){
                    std::unique_lock<std::mutex> lock(clientFdsLock);
                    perror("Nickname setup message failed");
                    clientFds.erase(clientFd);
                }
            }
            else if(r > 16){
                const char* msg = "Nickname too long ! Try something below 16 characters:\n";
                if(send(clientFd, msg, strlen(msg)+1, MSG_DONTWAIT) != (int)strlen(msg)+1){
                    perror("Nickname setup message failed");
                    std::unique_lock<std::mutex> lock(clientFdsLock);
                    clientFds.erase(clientFd);
                }
            }
            else
            {
                const char* msg = "Nickname already taken ! Try something different:\n";
                if(send(clientFd, msg, strlen(msg)+1, MSG_DONTWAIT) != (int)strlen(msg)+1){
                    perror("Nickname setup message failed");
                    std::unique_lock<std::mutex> lock(clientFdsLock);
                    clientFds.erase(clientFd);
                }
            }
        }

        else{
            printf("Client %d has disconnected !\n", clientFd);
            std::unique_lock<std::mutex> lock(clientFdsLock);
            clientFds.erase(clientFd);
            break;
        }
    }
}

// setPlayerNickname utility function
// FINISHED
bool validNickname(std::string nickname){
    for(int i : clientFds){
        // return false if name is already taken
        if (nickname == players[i].getNickname().c_str()){
            return false;
        }
    }
    return true;
}


//
void displayPlayers(){
    printf(" === Players online: %d===\n",playersConnected);
    for( int i : clientFds){
        printf("%s\n",players[i].getNickname().c_str());
    }
}


// Sends questions and possible answears then handles answears from clients
void askQuestion(Question q,std::unordered_set<int> players,int ownerFd){
        gameRooms.find(123*ownerFd)->second.playerAnswearsCount = 0;
        questionHandler(q,players);
        answearHandler(q,players,ownerFd);
    }

// Send question and possible answears to a group of players
// FINISHED
void questionHandler(Question q,std::unordered_set<int> players){
    int res;
    //std::unique_lock<std::mutex> lock(clientFdsLock);
    decltype(players) bad;
    for(int clientFd : players){
        //printf("Question handler loop playerfd : %d\n",clientFd);
        char msg[2048] = "\0";
        strcat(msg,q.questionText.c_str());
        strcat(msg,"\nA: ");
        strcat(msg,q.answearA.c_str());
        strcat(msg,"\nB: ");
        strcat(msg,q.answearB.c_str());
        strcat(msg,"\nC: ");
        strcat(msg,q.answearC.c_str());
        strcat(msg,"\nD: ");
        strcat(msg,q.answearD.c_str());
        strcat(msg,"\n");
        int count = strlen(msg);
        //printf("Size of question : %d\n", count);
        res = send(clientFd, msg, count, MSG_DONTWAIT);
        if(res!=count)
            bad.insert(clientFd);
    }
    for(int clientFd : bad){
        printf("removing %d\n", clientFd);
        clientFds.erase(clientFd);
        players.erase(clientFd);
        close(clientFd);
    }
}

// Collects answears from a group of players
void answearHandler(Question q,std::unordered_set<int> players_set,int ownerFd){
    auto start = high_resolution_clock::now();
    for(int clientFd : players_set){
        std::thread([clientFd,q,ownerFd,start]{
            //printf("Answear handler player %d",clientFd);
            char buff[32] = "\0";
            /*
            int count = read(clientFd,buff,32);
            if (count < 0){
                perror("read error, line 379");
                printf("removing %d\n", clientFd);
                clientFds.erase(clientFd);
                //players_set.erase(clientFd);
                close(clientFd);
            }
            */
                //char buffer[255] ="\0";
                //char* result;
            auto stop = high_resolution_clock::now();
            auto duration = duration_cast<seconds>(stop - start);
            while(duration.count() < q.answearTime){
                if(recv(clientFd,buff,255,MSG_DONTWAIT) > 0){
                    break;                
                }
                stop = high_resolution_clock::now();
                duration = duration_cast<seconds>(stop - start);
                sleep(0.1);
            }
            auto ansTime = duration_cast<microseconds>(stop - start)/1000;
            //auto stop = high_resolution_clock::now(); 
            //auto duration = duration_cast<microseconds>(stop - start)/1000; 
            buff[strlen(buff)-1] = '\0';
            //printf("The answear is: %s\n",q.correctAnswear.c_str());
            //printf("Player %s gave an answear (%s)\n",players[clientFd].getNickname().c_str(),buff);
            if(strcmp(buff,q.correctAnswear.c_str()) == 0){
                        char msg[2048] = "\0";
                        strcat(msg,"Player ");
                        strcat(msg,players[clientFd].getNickname().c_str());
                        strcat(msg," has answeared correctly in ");
                        strcat(msg,std::to_string(ansTime.count()).c_str());
                        strcat(msg," seconds\n");
                        int count = strlen(msg);
                        int res = send(ownerFd, msg, count, MSG_DONTWAIT);
                        if(res!=count){
                            printf("removing %d\n", clientFd);
                            clientFds.erase(clientFd);
                            close(clientFd);
                        }
                int score = 1000 +(1000*q.answearTime - ansTime.count())/50;
                players[clientFd].addToScore(score);
                printf("Player %s answeared correctly\n",players[clientFd].getNickname().c_str());
            }
            else{
                char msg[2048] = "\0";
                strcat(msg,"Player ");
                strcat(msg,players[clientFd].getNickname().c_str());
                strcat(msg," gave a wrong answear\n");

                int count = strlen(msg);
                int res = send(ownerFd, msg, count, MSG_DONTWAIT);
                if(res!=count){
                    printf("removing %d\n", clientFd);
                    clientFds.erase(clientFd);
                    close(clientFd);
                }
                //printf("Player %s gave a wrong answear\n",players[clientFd].getNickname().c_str());
            }
            //printf("Question answearing thread ended for player %d\n",clientFd);
            gameRooms.find(123*ownerFd)->second.playerAnswearsCount++;
            notifyFd = clientFd;
            controlQuestionsCv.notify_all();
            
        }).detach();   
    }
}


void handleLeave(int clientFd){
    char buffer[255] = "";
    while(players[clientFd].getWaiting()){
        recv(clientFd,buffer,255,MSG_DONTWAIT);
        if(strcmp(buffer,"3\n") == 0){
            players[clientFd].setWaiting(false);
            notifyFdMutex.lock();
            notifyRoomMutex.lock();
            notifyFd = clientFd;
            notifyRoomId = 0;
            startGameCv.notify_all();
            notifyFdMutex.unlock();
            notifyRoomMutex.unlock();
        }
        sleep(1);
    }
}

void sendScoreBoard(std::unordered_set<int> playersInRoom){
    std::map<int,int> playerScores;
    char scoreBoardMsg[2048] = "Scoreboard:\n";

    // sort player fd's by their score
    for(int clientFd : playersInRoom){
        // make score negative so the map sorts it in descending order
        playerScores.insert(std::pair<int, int>(-players[clientFd].getScore(),clientFd));
    }
    int count = 1;
    int lastScore;
    for (std::pair<int, int> p : playerScores){

        strcat(scoreBoardMsg,std::to_string(count).c_str());
        strcat(scoreBoardMsg,". ");
        strcat(scoreBoardMsg,players[p.second].getNickname().c_str());
        strcat(scoreBoardMsg," ");
        strcat(scoreBoardMsg,std::to_string(players[p.second].getScore()).c_str());
        strcat(scoreBoardMsg," points\n");
        lastScore = players[p.second].getScore();
        count++;
        if(count > 3)
            break;
    }

    for(int clientFd : playersInRoom){

        if(send(clientFd, scoreBoardMsg, strlen(scoreBoardMsg)+1, MSG_DONTWAIT) != (int)strlen(scoreBoardMsg) + 1){
            std::unique_lock<std::mutex> lock(clientFdsLock);
            perror("send error (score board)");
            clientFds.erase(clientFd);
        }
        if(players[clientFd].getScore() < lastScore){
            char yourScoreMsg[2048] = "Your score: ";
            strcat(yourScoreMsg,std::to_string(players[clientFd].getScore()).c_str());
            strcat(yourScoreMsg," points\n");

            if(send(clientFd, yourScoreMsg, strlen(yourScoreMsg)+1, MSG_DONTWAIT) != (int)strlen(yourScoreMsg) + 1){
                std::unique_lock<std::mutex> lock(clientFdsLock);
                perror("send error (score board)");
                clientFds.erase(clientFd);
            }
        }
    }
}   


void createQuiz(int clientFd){
        char createQuizMsg[] = "Enter quiz title: \n";
        Quiz quiz;
        
        if(send(clientFd, createQuizMsg, strlen(createQuizMsg)+1, MSG_DONTWAIT) != (int)strlen(createQuizMsg) + 1){
        std::unique_lock<std::mutex> lock(clientFdsLock);
        perror("Send error (menu)");
        clientFds.erase(clientFd);
        }
        char buffer[4096];
        memset(buffer,0,4096);
        int count = read(clientFd,buffer,4096);
        if(count < 0){
            perror("Read error (menu)");
            std::unique_lock<std::mutex> lock(clientFdsLock);
            clientFds.erase(clientFd);
            playersConnected --;
        }
        buffer[count-1] = '\0';
        quiz.quizTitle = buffer;
        //printf("%s\n",quiz.quizTitle.c_str());
        int questionCount = 0;
        
        do{
            Question newQuestion;
            questionCount++;
            char createQuizMsg[4096] = "Enter question text (question no. ";
            strcat(createQuizMsg,std::to_string(questionCount).c_str());
            strcat(createQuizMsg,")\n");
            if(send(clientFd, createQuizMsg, strlen(createQuizMsg)+1, MSG_DONTWAIT) != (int)strlen(createQuizMsg) + 1){
            std::unique_lock<std::mutex> lock(clientFdsLock);
            perror("Send error (menu)");
            clientFds.erase(clientFd);
            }
            memset(buffer,0,4096);
            count = read(clientFd,buffer,4096);
            if(count < 0){
                perror("Read error (menu)");
                std::unique_lock<std::mutex> lock(clientFdsLock);
                clientFds.erase(clientFd);
                playersConnected --;         
            }
            buffer[count-1] = '\0';
            newQuestion.questionText = buffer;

            strcpy(createQuizMsg,"Enter answear A text:\n");
            if(send(clientFd, createQuizMsg, strlen(createQuizMsg)+1, MSG_DONTWAIT) != (int)strlen(createQuizMsg) + 1){
            std::unique_lock<std::mutex> lock(clientFdsLock);
            perror("Send error (menu)");
            clientFds.erase(clientFd);
            }
            memset(buffer,0,4096);
            count = read(clientFd,buffer,4096);
            if(count < 0){
                perror("Read error (menu)");
                std::unique_lock<std::mutex> lock(clientFdsLock);
                clientFds.erase(clientFd);
                playersConnected --;         
            }
            buffer[count-1] = '\0';
            newQuestion.answearA = buffer;

            strcpy(createQuizMsg,"Enter answear B text:\n");
            if(send(clientFd, createQuizMsg, strlen(createQuizMsg)+1, MSG_DONTWAIT) != (int)strlen(createQuizMsg) + 1){
            std::unique_lock<std::mutex> lock(clientFdsLock);
            perror("Send error (menu)");
            clientFds.erase(clientFd);
            }
            memset(buffer,0,4096);
            count = read(clientFd,buffer,4096);
            if(count < 0){
                perror("Read error (menu)");
                std::unique_lock<std::mutex> lock(clientFdsLock);
                clientFds.erase(clientFd);
                playersConnected --;         
            }
            buffer[count-1] = '\0';
            newQuestion.answearB = buffer;

            strcpy(createQuizMsg,"Enter answear C text:\n");
            if(send(clientFd, createQuizMsg, strlen(createQuizMsg)+1, MSG_DONTWAIT) != (int)strlen(createQuizMsg) + 1){
                std::unique_lock<std::mutex> lock(clientFdsLock);
                perror("Send error (menu)");
                clientFds.erase(clientFd);
            }
            memset(buffer,0,4096);
            count = read(clientFd,buffer,4096);
            if(count < 0){
                perror("Read error (menu)");
                std::unique_lock<std::mutex> lock(clientFdsLock);
                clientFds.erase(clientFd);
                playersConnected --;         
            }
            buffer[count-1] = '\0';
            newQuestion.answearC = buffer;

            strcpy(createQuizMsg,"Enter answear D text:\n");
            if(send(clientFd, createQuizMsg, strlen(createQuizMsg)+1, MSG_DONTWAIT) != (int)strlen(createQuizMsg) + 1){
            std::unique_lock<std::mutex> lock(clientFdsLock);
            perror("Send error (menu)");
            clientFds.erase(clientFd);
            }
            memset(buffer,0,4096);
            count = read(clientFd,buffer,4096);
            if(count < 0){
                perror("Read error (menu)");
                std::unique_lock<std::mutex> lock(clientFdsLock);
                clientFds.erase(clientFd);
                playersConnected --;         
            }
            buffer[count-1] = '\0';
            newQuestion.answearD = buffer;

            strcpy(createQuizMsg,"Which answear is correct? (A,B,C,D)\n");
            if(send(clientFd, createQuizMsg, strlen(createQuizMsg)+1, MSG_DONTWAIT) != (int)strlen(createQuizMsg) + 1){
                std::unique_lock<std::mutex> lock(clientFdsLock);
                perror("Send error (menu)");
                clientFds.erase(clientFd);
            }
            
            do{
                memset(buffer,0,4096);
                count = read(clientFd,buffer,4096);
                if(count < 0){
                    perror("Read error (menu)");
                    std::unique_lock<std::mutex> lock(clientFdsLock);
                    clientFds.erase(clientFd);
                    playersConnected --;         
                } 
            }while(strcmp(buffer,"A\n") != 0 && strcmp(buffer,"B\n") != 0 && strcmp(buffer,"C\n") != 0 && strcmp(buffer,"D\n") != 0);
            buffer[count-1] = '\0';
            newQuestion.correctAnswear = buffer;

            // only 20 seconds per question. period.
            newQuestion.answearTime = 20;
            quiz.addQuestion(newQuestion);

            do{
            strcpy(createQuizMsg,"Create another question - type \"1\"\nFinish quiz - type \"2\"\n");
            if(send(clientFd, createQuizMsg, strlen(createQuizMsg)+1, MSG_DONTWAIT) != (int)strlen(createQuizMsg) + 1){
                std::unique_lock<std::mutex> lock(clientFdsLock);
                perror("Send error (menu)");
                clientFds.erase(clientFd);
            }
            memset(buffer,0,4096);
            count = read(clientFd,buffer,4096);
            if(count < 0){
                    perror("Read error (menu)");
                    std::unique_lock<std::mutex> lock(clientFdsLock);
                    clientFds.erase(clientFd);
                    playersConnected --;         
                }
            }while(strcmp(buffer,"1\n") != 0 && strcmp(buffer,"2\n") != 0);
        }while(strcmp(buffer,"2\n") != 0);
        quizSet.push_back(quiz);

        strcpy(createQuizMsg,"Quiz created!\n");
        if(send(clientFd, createQuizMsg, strlen(createQuizMsg)+1, MSG_DONTWAIT) != (int)strlen(createQuizMsg) + 1){
            std::unique_lock<std::mutex> lock(clientFdsLock);
            perror("Send error (menu)");
            clientFds.erase(clientFd);
        }      
}

void loadSampleQuizzes(){
    Question sampleQuestion;
    sampleQuestion.questionText = "A is the correct answear.";
    sampleQuestion.answearA = "Answear 1";
    sampleQuestion.answearB = "Answear 2";
    sampleQuestion.answearC = "Answear 3";
    sampleQuestion.answearD = "Answear 4";
    sampleQuestion.correctAnswear = "A";
    sampleQuestion.answearTime = 20;

    Quiz sampleQuizA;
    sampleQuizA.addQuestion(sampleQuestion);
    sampleQuizA.addQuestion(sampleQuestion);
    sampleQuizA.addQuestion(sampleQuestion);
    sampleQuizA.addQuestion(sampleQuestion);
    sampleQuizA.quizTitle = "Sample quiz A";

    sampleQuestion.questionText = "B is the correct answear.";
    sampleQuestion.answearA = "Answear 1";
    sampleQuestion.answearB = "Answear 2";
    sampleQuestion.answearC = "Answear 3";
    sampleQuestion.answearD = "Answear 4";
    sampleQuestion.correctAnswear = "B";
    sampleQuestion.answearTime = 20;

    Quiz sampleQuizB;
    sampleQuizB.addQuestion(sampleQuestion);
    sampleQuizB.addQuestion(sampleQuestion);
    sampleQuizB.addQuestion(sampleQuestion);
    sampleQuizB.quizTitle = "Sample quiz B";

    sampleQuestion.questionText = "C is the correct answear.";
    sampleQuestion.answearA = "Answear 1";
    sampleQuestion.answearB = "Answear 2";
    sampleQuestion.answearC = "Answear 3";
    sampleQuestion.answearD = "Answear 4";
    sampleQuestion.correctAnswear = "C";
    sampleQuestion.answearTime = 20;

    Quiz sampleQuizC;
    sampleQuizC.addQuestion(sampleQuestion);
    sampleQuizC.addQuestion(sampleQuestion);
    sampleQuizC.quizTitle = "Sample quiz C";

    quizSet.push_back(sampleQuizA);
    quizSet.push_back(sampleQuizB);
    quizSet.push_back(sampleQuizC);
}

bool isNumeric(std::string str) {
   for (int i = 0; i < (int)(str.length()); i++){
      if (isdigit(str[i]) == false)
         return false;
   }
    return true;
}