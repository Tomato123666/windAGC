#ifndef FR_TYPES_H
#define FR_TYPES_H

#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <string>
#include <sstream>

// NOTE: DO NOT use 'using namespace std;' — it causes std::byte vs Windows byte conflict in C++17

// Format double to string with fixed precision for display
inline std::string fmt(double v, int precision = 2)
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(precision);
    ss << v;
    return ss.str();
}

// Format time_t to HH:MM:SS
inline std::string fmt(time_t t)
{
    tm tmNow;
    localtime_s(&tmNow, &t);
    char buf[64];
    strftime(buf, sizeof(buf), "%H:%M:%S", &tmNow);
    return std::string(buf);
}

// Print timestamped log message
inline void printLog(const std::string& msg)
{
    time_t now = time(nullptr);
    tm tmNow;
    localtime_s(&tmNow, &now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmNow);
    std::cout << "[" << buf << "] " << msg << std::endl;
}

// ====================== ���ģ���Ƶϵͳ״̬�� ======================
// ����������Ƶϵͳ��4������״̬
enum FRState
{
    READY = 0,      // ������Ƶ���������޵�Ƶ����
    ACTIVE = 1,     // ��Ƶ���Ƶ��Խ�ޣ�����ִ��һ�ε�Ƶ
    RECOVERY = 2,   // �ָ�״̬��Ƶ�ʻع�50Hz��ת��/���ʻ����ָ�
    FAULT = 3       // ����״̬
};

// ====================== ���ģ����ʵ��ģ�� ======================
struct Turbine
{
    int id;                         // ������
    bool running = true;            // ����״̬
    bool fault = false;             // ���ϱ��
    double power = 1500.0;          // ���������
    double maxPower = 2500.0;       // ����������ʣ��޷��ã�
    double minPower = 300.0;        // ��С���繦��
    double rotorSpeed = 1.0;        // ת��ת�٣���Ƶ�ָ��׶�ʹ�ã�
};

// ====================== ���ģ���Ƶ����ָ�� ======================
struct FRCommand
{
    int turbineId;          // ָ�����̨���
    double basePower;       // ��������
    double frPower;         // ��Ƶ���ӹ���
    double finalPower;      // �����·����ܹ���ָ��
};

#endif