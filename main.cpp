#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <iostream>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <iostream>

#include "log.h"
#include "host.h"
#include "loadBalance.h"
#include "healthCheck.h"

static const char* CONFIG_FILE_PATH = "config";
static std::vector<Host*> g_logicalSrvs;
static std::string g_localHostName;
static int g_localPort;
static int g_checkInter; //健康检查时间间隔
static int g_checkRise;  //服务器恢复服务需要的连接成功次数
static int g_checkFall;  //服务器停止服务需要的连接失败次数


int readConfig(){ //读配置文件
    std::ifstream in(CONFIG_FILE_PATH); 
    if(!in.is_open()){
        return -1;
    }
    std::string line;
    while(!in.eof()){
        getline(in, line);
        if(line.compare(0, 1, "#") == 0){  //忽略注释
            continue;
        }else if(line.compare(0, 6, "listen") == 0){
            size_t pos = line.find(' ', 7);
            g_localHostName = line.substr(7, pos - 7);
            g_localPort = std::stoi(line.substr(pos + 1));
        }else if(line.compare(0, 6, "server") == 0){
            size_t pos1 = line.find(' ', 7);
            size_t pos2 = line.find(' ', pos1 + 1);
            std::string hostName = line.substr(7, pos1 - 7);
            int port = std::stoi(line.substr(pos1 + 1, pos2 - pos1 - 1));
            int conns = std::stoi(line.substr(pos2 + 1));
            Host* tmpHost = new Host(hostName, port, conns);
            g_logicalSrvs.push_back(tmpHost);
        }else if(line.compare(0, 5, "inter") == 0){
            g_checkInter = std::stoi(line.substr(6));
        }else if(line.compare(0, 4, "rise") == 0){
            g_checkRise = std::stoi(line.substr(5));
        }else if(line.compare(0, 4, "fall") == 0){
            g_checkFall = std::stoi(line.substr(5));
        }
    }
    in.close();
    return 0;
}

void signalHandler(int sigNum){
    for(auto server : g_logicalSrvs){
        delete server;
    }
    exit(sigNum);
}

int main(){
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    if(readConfig() == -1){
        log(LOG_ERR, __FILE__, __LINE__, "%s", "open config file fail");
        return 1;
    }
        
    int listenFd = socket(PF_INET, SOCK_STREAM, 0); //start listening
    assert(listenFd >= 0);
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    char* ip = (char*)g_localHostName.c_str();
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(g_localPort);
    int ret = bind(listenFd, (struct sockaddr*)&address, sizeof(address));
    assert(ret != -1);
    ret = listen(listenFd, 5);
    assert(ret != -1);
    
    LoadBalance loadBalance(listenFd, g_logicalSrvs);
    HealthCheck healthCheck(g_logicalSrvs, g_checkInter, g_checkRise, g_checkFall);
    std::thread t1(&LoadBalance::balance, loadBalance); //open a thread for load balance
    std::thread t2(&HealthCheck::check, healthCheck);  //open a thread for health check
    t1.join();
    t2.join();
    
    return 0;
}

