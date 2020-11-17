#ifndef PRIMEIIDRIVER_H
#define PRIMEIIDRIVER_H
#include <windows.h>
#include <functional>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <conio.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <deque>
#include <map>

#include "HermesSDK.h"
#include "concurrentqueue.h"

#define FINGERCOUNT           5
#define JOINTPERFINGERCOUNT   3
#define TIMEBETWEENHAPTICSCMDS_MS  20

static const std::array<std::string, FINGERCOUNT> fingerNames = { "thumb", "index", "middle", "ring", "pinky" };
static const std::array<std::string, JOINTPERFINGERCOUNT> fingerJointNames = { "mcp", "pip", "dip" };
static const std::array<std::string, JOINTPERFINGERCOUNT> thumbJointNames = { "cmc", "mcp", "ip" };

class PrimeIIDriver
{
public:
   
  enum HandType : int { Unknown = 0, LeftHand = 1, RightHand = 2};
  
  //struct FingerJointFlex
  //{
  //  float mcpSpread;
  //  float mcpStretch;
  //  float pipStretch;
  //  float dipStretch;
  //};
  //struct ThumbJointFlex
  //{
  //  float cmcSpread;
  //  float cmcStretch;
  //  float mcpStretch;
  //  float ipStretch;
  //};

  struct FingerFlex
  {
    float Joint1Spread;
    float Joint1Stretch;
    float Joint2Stretch;
    float Joint3Stretch;
  };

  struct Quaternion
  {
    float x;
    float y;
    float z;
    float w;
  };

  struct Fingers
  {
    std::array<PrimeIIDriver::FingerFlex, FINGERCOUNT> fingersFlex;
    std::array<PrimeIIDriver::Quaternion, FINGERCOUNT> fingersIMU;
  };

  struct GloveData
  {
    uint32_t deviceid;
    uint32_t dongleid;
    HandType handtype;
    Fingers  fingers;
    Quaternion wristIMU;
  };

private:
  struct VibrateFingers
  {
    bool sendFlage;
    uint32_t dongleId;
    Hermes::Protocol::HandType handtype;
    std::array<float, 5> powers{};
  };
  
public:
  PrimeIIDriver(const std::string& _clientName, const std::string& _clientInfo);
  PrimeIIDriver(const std::string& _clientName, const std::string& _clientInfo, const std::string& _address);
  ~PrimeIIDriver();
   
  void start();
  void stop();
  void join();

  std::vector<PrimeIIDriver::GloveData> getGlovesData();
  
  bool setVibrateFingers(uint32_t _dongleId, PrimeIIDriver::HandType _handtype, std::array<float, 5> _powers);

private:
  std::thread* td;
  bool requestExit;

  std::vector<PrimeIIDriver::GloveData> GlovesQueue;

  std::mutex m_Landscape_mutex;
  Hermes::Protocol::Hardware::DeviceLandscape m_Landscape;

  std::mutex m_DeviceData_mutex;
  moodycamel::ConcurrentQueue<Hermes::Protocol::Devices> m_DevicesData;

  std::mutex m_FingersVibrate_mutex;
  VibrateFingers m_vf;
  std::chrono::high_resolution_clock::time_point m_timeLastHapticsCmdSent;

  //HermesSDK CallBack
  HermesSDK::filterSetupCallback onFilterSetup = [&](Hermes::Protocol::Pipeline& _pipeline)
  {
    _pipeline.set_name("C++ sample pipeline");
    _pipeline.clear_filters();
    auto CreepCompensationFilter = _pipeline.add_filters();
    CreepCompensationFilter->set_name("CreepCompensation");
    auto basisConversion = _pipeline.add_filters();
    basisConversion->set_name("BasisConversion");
    auto basisConversionParamSet = new Hermes::Protocol::ParameterSet();
    auto basisConversionParam = basisConversionParamSet->add_parameters();
    basisConversionParam->set_name("serializedMeshConfig");
    std::string bytes;
    createMeshConfig(&bytes);
    basisConversionParam->set_bytes(bytes);
    basisConversion->set_allocated_parameterset(basisConversionParamSet);	
  };
  HermesSDK::deviceDataCallback onDeviceData = [&](const Hermes::Protocol::Devices& _data) 
  {
    m_DeviceData_mutex.lock();
    m_DevicesData.enqueue(_data);
    m_DeviceData_mutex.unlock();
  };
  HermesSDK::deviceLandscapeCallback onLandscapeData = [&](const Hermes::Protocol::Hardware::DeviceLandscape& _data)
  {
    m_Landscape_mutex.lock();
    m_Landscape = _data;
    m_Landscape_mutex.unlock();
  };
  HermesSDK::errorMessageCallback onError = [&](const HermesSDK::ErrorMessage& _msg)
  {
    //TODO
    //spdlog::error("onError: {}", _msg.errorMessage);
  };
  
  void update();

  void ProcessDeviceData();
  void ProcessLandscapeData();
  void ProcessFingersVibrate();

  float roundFloat(float val, int places);

  bool ConnectLocal(const std::string& _clientName, const std::string& _clientInfo);
  bool ConnectNetworkByAddress(const std::string& _clientName, const std::string& _clientInfo, const std::string& _address);

  void createMeshConfig(std::string* _bytes);
  Hermes::Protocol::MeshNodeConfig* createMeshNodeConfig(Hermes::Protocol::coor_axis_t _up, Hermes::Protocol::coor_axis_t _forward, Hermes::Protocol::coor_axis_t _right);

};

#endif // !PRIMEIIDRIVER_H


