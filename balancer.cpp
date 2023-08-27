// Балансировщик нагрузки
#include <fstream>
#include <vector>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <fcntl.h>
#include <string.h>
using namespace std;

#define MAXLINE 128 // размер буфера

// структура для чтения данных
// из конфигурационного файла
struct config
{
    int port;
    vector<int> serv_addrs;
    int max_datagrams;
};

// дескрипторы каналов для взаимодействия
// между потоками
int pipefd1[2], pipefd2[2];

// Поток собственно балансировщика - принцип работы:
// - Балансировщик принимает датаграммы от клиента
// - Получает значение timer от таймера
// - Если предыдущее значение timer больше, чем текущее
// (т.e. если текущая секунда еще не прошла) и если
// в эту секунду не было отправлено больше N датаграмм,
// то посылает эту ДГ серверу, иначе ДГ "выкидывается"
// - Поочередно отправляет ДГ каждому из серверов
// (например, если прошлая ДГ была отправлена 1-му серверу,
// то текущая - 2-му)
void *balancer(void *conf)
{
    config *confs = (config*) conf;
    sockaddr_in servaddr, cliaddr;
    int sd;

    // создаем сокет для взаимодействия с клиентом
    if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket creation error");
        exit(EXIT_FAILURE);
    }

    // очищаем адреса балансировщика и клиента
    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

	// заполняем информацию о балансировщике
	servaddr.sin_family = AF_INET; // протокол (IPv4)
	servaddr.sin_addr.s_addr = INADDR_ANY; // связка с локальными интерфейсами
	servaddr.sin_port = htons(confs->port); // порт балансировщика

    // связываем сокет с адресом балансировщика
    if (bind(sd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("bind error");
        exit(EXIT_FAILURE);
    }

    // начинаем принимать датаграммы от клиента
    socklen_t len = sizeof(cliaddr);
    char buffer[MAXLINE];
    int i = 1, n = 0;
    float oldTimer = 0, newTimer, diff;
    int signal = 2;
    while (true)
    {
        // принимаем ДГ
        if (recvfrom(sd, buffer, sizeof(buffer), 0, (sockaddr*)&cliaddr, &len) < 0)
            perror("receive error");

        write(pipefd2[1], &signal, sizeof(signal)); // посылаем сигнал таймеру
        read(pipefd1[0], &newTimer, sizeof(newTimer)); // получаем значение timer

        diff = newTimer - oldTimer; 
        oldTimer = newTimer;
        ++n;

        // если за текущую секунду было переправлено не более N ДГ,
        // то переправляем текущую ДГ
        if (n <= confs->max_datagrams)
        {
            cout << "Redirecting " << buffer << " to server " << i << endl;

            int sd_serv;
            sockaddr_in servaddr_s, cliaddr_s;

            // создаем сокет для взаимодействия с сервером
            if ((sd_serv = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
            {
                perror("socket creation error");
                exit(EXIT_FAILURE);
            }

            memset(&servaddr_s, 0, sizeof(servaddr_s));

            servaddr_s.sin_family = AF_INET;
            servaddr_s.sin_addr.s_addr = INADDR_ANY;
            servaddr_s.sin_port = htons(confs->serv_addrs[i - 1]); // адрес из конф. файла

            // отправляем ДГ серверу
            sendto(sd_serv, (const char *) buffer, strlen(buffer),
            MSG_CONFIRM, (const struct sockaddr *) &servaddr_s,
                sizeof(servaddr_s));

            // закрываем "серверный" сокет
            close(sd_serv);

            // меняем сервер назначения
            i = i == confs->serv_addrs.size()? 1: ++i;
        }

        // если текущая секунда закончена, то
        // обнуляем кол-во переправленных ДГ за секунду 
        if (diff < 0)
            n = 0;
    }
    
    close(sd); // закрываем "клиентный" сокет
    pthread_exit((void*)2); // выходим из потока
}

// Поток таймера - принцип работы:
// - Переменная timer изменяется с 0 до 1 каждую секунду
// - Как только она достигнет 1 (т.e. когда пройдет текущая секунда), 
// она меняется на 0
// - Когда балансировщик принимает датаграмму, он посылает
// сигнал таймеру
// - Если сигнал получен, таймер посылает балансировщику
// значение timer
// - На основе значения timer принимается решение о том,
// отправлять ли серверу данную датаграмму
void *timer(void *arg)
{
    float timer = 0.000f;
    int signal = -1;
    while(true)
    {
        // каждую мс увеличиваем timer
        // (ввиду скорости работы ПК значение 800 в usleep
        // больше соответствует 1 секунде, чем 1000)
        timer += 0.001;
        usleep(800);
        if (timer >= 1.000)
            timer = 0.000f;
        read(pipefd2[0], &signal, sizeof(signal)); // читаем сигнал
        if (signal == 2)
        {
            write(pipefd1[1], &timer, sizeof(timer)); // отправляем значение timer
            signal = -1;
        }

    }
    pthread_exit((void*)2); // выходим из потока
}

int main()
{
    ifstream file;
    string line;
    config conf;

    // читаем данные из конф. файла
    file.open("config.conf");
    if (file.is_open())
    {
        int i = 0;
        while (getline(file, line))
        {
            switch (i)
            {
                // первая строка - порт балансировщика
                case 0:
                    conf.port = stoi(line);
                    break;
                // вторая строка - адреса серверов
                case 1:
                {
                    stringstream ss(line);
                    char ch;
                    int tmp;
                    while (ss >> tmp) 
                    {
                        conf.serv_addrs.push_back(tmp);
                        ss >> ch;
                    }
                    break;
                }
                // третья строка - макс. кол-во датаграмм в секунду
                case 2:
                    conf.max_datagrams = stoi(line);
                    break;
                default:
                    break;
            }
            ++i;
        }
        file.close();
    }
    else
        cout << "Unable to open config file." << endl;

    // создаем два неименованных канала для
    // взаимодействия потоков
    pthread_t id1, id2;
    int* exitcode;
    pipe2(pipefd1, O_NONBLOCK | O_DIRECT);
    pipe2(pipefd2, O_NONBLOCK | O_DIRECT);

    // создаем потоки для балансировщика и таймера
    pthread_create(&id1, NULL, balancer, &conf);
    pthread_create(&id2, NULL, timer, 0);

    // по завершении уничтожаем потоки...
    pthread_join(id1, (void**) &exitcode);
    pthread_join(id2, (void**) &exitcode);

    // ...и закрываем каналы
    close(pipefd1[0]);
    close(pipefd1[1]);
    close(pipefd2[0]);
    close(pipefd2[1]);
    return 0;
}