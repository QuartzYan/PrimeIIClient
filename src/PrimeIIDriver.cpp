#include "../include/PrimeIIDriver.h"

PrimeIIDriver::PrimeIIDriver(const std::string& _clientName, const std::string& _clientInfo)
  :requestExit(false)
{
  td = NULL;
  m_vf.sendFlage = false;
  ConnectLocal(_clientName, _clientInfo);
}

PrimeIIDriver::PrimeIIDriver(const std::string& _clientName, const std::string& _clientInfo, const std::string& _address)
  : requestExit(false) 
{
  td = NULL;
  m_vf.sendFlage = false;
  ConnectNetworkByAddress(_clientName, _clientInfo, _address);
}

PrimeIIDriver::~PrimeIIDriver()
{
  stop();
  join();
  HermesSDK::Stop();
  if (td != NULL)
  {
    delete td;
    td = NULL;
  }
}

bool PrimeIIDriver::setVibrateFingers(uint32_t _dongleId, PrimeIIDriver::HandType _handtype, const std::array<float, 5> _powers)
{
  std::lock_guard<std::mutex> lck(m_FingersVibrate_mutex);

  m_vf.sendFlage = false;

  for (size_t i = 0; i < _powers.size(); i++)
  {
    if (_powers.at(i) > 1.0)
    {
      return false;
    }
    else
    {
      m_vf.powers.at(i) = _powers.at(i);
    }
  }

  m_vf.dongleId = _dongleId;
 
  switch (_handtype)
  {
  case PrimeIIDriver::HandType::LeftHand:
    m_vf.handtype = Hermes::Protocol::HandType::Left;
    break;
  case PrimeIIDriver::HandType::RightHand:
    m_vf.handtype = Hermes::Protocol::HandType::Right;
    break;
  default:
    m_vf.handtype = Hermes::Protocol::HandType::UnknownChirality;
    break;
  }
  
  m_vf.sendFlage = true;

  m_timeLastHapticsCmdSent = std::chrono::high_resolution_clock::now();

  return true;

  //HermesSDK::VibrateWrist(1819774777, 0.5, 20);
  //return HermesSDK::VibrateFingers(_dongleId, m_vf.handtype, _powers);
}

void PrimeIIDriver::start()
{
  requestExit = false;
  td = new std::thread(std::bind(&PrimeIIDriver::update, this));
}

void PrimeIIDriver::stop()
{
  requestExit = true;
}

void PrimeIIDriver::join()
{
  td->join();
}

std::vector<PrimeIIDriver::GloveData> PrimeIIDriver::getGlovesData()
{
  std::lock_guard<std::mutex> lck(m_DeviceData_mutex);
  return GlovesQueue;
}

void PrimeIIDriver::update()
{
  while (!requestExit)
  {
    if (!HermesSDK::IsRunning())
    {
      std::cout << "Waiting to connect to the Manus Core..." << std::endl;
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      continue;
    }

    //process data
    ProcessDeviceData();
    ProcessLandscapeData();

    //process handle rumble
    ProcessFingersVibrate();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

void PrimeIIDriver::ProcessLandscapeData()
{
  std::lock_guard<std::mutex> lck(m_Landscape_mutex);

  for (auto& forestkv : m_Landscape.forest())
  {
    Hermes::Protocol::Hardware::DeviceForest::ForestType forestType = forestkv.second.foresttype();

    std::string forestName = forestkv.first;

    for (auto& treekv : forestkv.second.trees())
    {
      bool isDeviceForest = forestType == Hermes::Protocol::Hardware::DeviceForest::ForestType::DeviceForest_ForestType_DevicesForest;
      bool isHapticsForest = forestType == Hermes::Protocol::Hardware::DeviceForest::ForestType::DeviceForest_ForestType_HapticsForest;

      if (isDeviceForest)
      {
        std::string family = FamilyInfo::FamilyToString(treekv.second.family());
        //treekv.first, treekv.second.name(), treekv.second.description(), treekv.second.channel(), family;
      }
      else if (isHapticsForest)
      {
        //treekv.first, treekv.second.name(), treekv.second.description();
      }
      else
      {
        //treekv.second.name(), treekv.second.description();
      }

      for (auto& leafkv : treekv.second.leafs())
      {
        auto leaf = leafkv.second;
        // only print paired gloves
        if (leaf.paired())
        {
          LeafInfo info = HermesSDK::GetLeafInfo(leaf);
          if (info.DeviceOfType() != LeafInfo::DeviceType::HapticsModule)
          {
            //info.Name(), info.Description(), info.BatteryPercentage(), info.TransmissionStrength(), info.FamilyToString();
          }
          else
          {
            //info.Name(), info.Description(), info.BatteryPercentage(), info.TransmissionStrength();
          }
        }
      }
    }
  }
}

void PrimeIIDriver::ProcessFingersVibrate()
{
  std::lock_guard<std::mutex> lck(m_FingersVibrate_mutex);

  if (m_vf.sendFlage)
  {
    unsigned long long elapsedLastCmd_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - m_timeLastHapticsCmdSent).count();

    if (elapsedLastCmd_ms < TIMEBETWEENHAPTICSCMDS_MS)
    {
      HermesSDK::VibrateFingers(m_vf.dongleId, m_vf.handtype, m_vf.powers);
      m_vf.sendFlage = true;
    }
    else
    {
      m_vf.sendFlage = false;
    }
    //HermesSDK::VibrateFingers(m_vf.dongleId, m_vf.handtype, m_vf.powers);
    //m_vf.sendFlage = false;
  }
}

void PrimeIIDriver::ProcessDeviceData()
{
  std::lock_guard<std::mutex> lck(m_DeviceData_mutex);

  if (m_DevicesData.size_approx() == 0)
    return;

  //get the latest data, discard the rest
  Hermes::Protocol::Devices dev;
  while (m_DevicesData.try_dequeue(dev))
  {
    continue;
  }

  GlovesQueue.clear();

  if (dev.gloves_size() > 0)
  {
    for (int i = 0; i < dev.gloves_size(); i++)
    {
      PrimeIIDriver::GloveData gloveData;

      auto glove = dev.gloves(i);

      //process info data
      if (glove.has_info())
      {
        auto info = glove.info();

        //const uint64_t gloveId = info.deviceid();
        //std::stringstream stream;
        //stream << std::hex << std::uppercase << gloveId;
        //std::string gloveIdString("0x" + stream.str());
        //std::cout << gloveIdString << std::endl;
        //std::cout << "Hardware glove device id." << info.deviceid() << std::endl;
        //std::cout << "Hardware dongle device id." << info.dongleid() << std::endl;
        //std::cout << "Left, right or unknown." << info.handtype() << std::endl;
        //std::cout << "PrimeOne or PrimeTwo?" << info.gloveversion() << std::endl;
        gloveData.deviceid = info.deviceid();
        gloveData.dongleid = info.dongleid();
        switch (info.handtype())
        {
        case Hermes::Protocol::HandType::Left:							gloveData.handtype = PrimeIIDriver::HandType::LeftHand;           break;
        case Hermes::Protocol::HandType::Right:							gloveData.handtype = PrimeIIDriver::HandType::RightHand;          break;
        case Hermes::Protocol::HandType::UnknownChirality:	default:	gloveData.handtype = PrimeIIDriver::HandType::Unknown;  break;
        }
      }

      //process info data
      auto raw = glove.raw();
      for (int i = 0; i < raw.flex_size(); i++)
      {
        auto rawfinger = raw.flex(i);
        auto quatfinger = glove.fingers(i);

        //使用imus进行手指摇摆，是相对于腕部IMU的旋转，而非相对于世界
        //imu(0) = wrist, imu(1) = thumb, imu(2) = index, imu(3) = middle, imu(4) = ring, imu(5) = pinky
        //primeOne has 2 imu's (wrist and thumb), primeTwo has 6 imu's (wrist + 5 fingers)
        Hermes::Protocol::Orientation imu;
        int imu_nr = i + 1; 
        if (raw.imus_size() > imu_nr) 
        {
          imu = raw.imus(imu_nr);
          gloveData.fingers.fingersIMU[i].x = roundFloat(imu.full().x(), 3);
          gloveData.fingers.fingersIMU[i].y = roundFloat(imu.full().y(), 3);
          gloveData.fingers.fingersIMU[i].z = roundFloat(imu.full().z(), 3);
          gloveData.fingers.fingersIMU[i].w = roundFloat(imu.full().w(), 3);
        }

        //bool thumb = (i == 0);
        //const std::array<std::string, JOINTPERFINGERCOUNT>& jointNames = thumb ? thumbJointNames : fingerJointNames;
        //fingerNames[i]; //手指的名字
        //jointNames[0]; //拇指为cmc，其他手指为mcp 
        //roundFloat(quatfinger.phalanges(0).spread(), 2);  //mcp联合手指摇摆标准化值
        //roundFloat(quatfinger.phalanges(0).stretch(), 2); //mcp关节手指弯曲标准化值，在flex和imu传感器之间混合
        //jointNames[1]; //拇指为mcp，其他手指为pip
        //roundFloat(quatfinger.phalanges(1).stretch(), 2); //pip关节手指弯曲归一化值
        //jointNames[2]; //拇指为ip，其他手指为dip
        //roundFloat(quatfinger.phalanges(2).stretch(), 2); //dip关节手指弯曲归一化值，无传感器和pip值相同

        gloveData.fingers.fingersFlex[i].Joint1Spread   =   roundFloat(quatfinger.phalanges(0).spread(), 2);
        gloveData.fingers.fingersFlex[i].Joint1Stretch  =   roundFloat(quatfinger.phalanges(0).stretch(), 2);
        gloveData.fingers.fingersFlex[i].Joint2Stretch  =   roundFloat(quatfinger.phalanges(1).stretch(), 2);
        gloveData.fingers.fingersFlex[i].Joint3Stretch  =   roundFloat(quatfinger.phalanges(2).stretch(), 2);
      }

      //get wrist quaternion
      auto wrist = glove.wrist();
      //std::cout << roundFloat(wrist.full().x(), 3) << ',' 
      //  << roundFloat(wrist.full().y(), 3) << ','
      //  << roundFloat(wrist.full().z(), 3) << ','
      //  << roundFloat(wrist.full().w(), 3) << std::endl;
      gloveData.wristIMU.x = roundFloat(wrist.full().x(), 3);
      gloveData.wristIMU.y = roundFloat(wrist.full().y(), 3);
      gloveData.wristIMU.z = roundFloat(wrist.full().z(), 3);
      gloveData.wristIMU.w = roundFloat(wrist.full().w(), 3);
    
      GlovesQueue.push_back(gloveData);
    }
  }
}

float PrimeIIDriver::roundFloat(float val, int places)
{
  return std::round(val * std::pow(10, places)) / std::pow(10, places);
}

bool PrimeIIDriver::ConnectLocal(const std::string& _clientName, const std::string& _clientInfo)
{
  HermesSDK::ConnectLocal(_clientName, _clientInfo, this->onFilterSetup, this->onDeviceData, this->onLandscapeData, this->onError);

  m_timeLastHapticsCmdSent = std::chrono::high_resolution_clock::now(); //初始化触觉时钟

  return true;
}

bool PrimeIIDriver::ConnectNetworkByAddress(const std::string& _clientName, const std::string& _clientInfo, const std::string& _address)
{
  HermesSDK::ConnectNetworkAddress(_clientName, _clientInfo, _address, this->onFilterSetup, this->onDeviceData, this->onLandscapeData, this->onError);

  m_timeLastHapticsCmdSent = std::chrono::high_resolution_clock::now(); // initialize the haptics clock

  return true;
}

void PrimeIIDriver::createMeshConfig(std::string* _bytes)
{
  auto meshConfig = new Hermes::Protocol::MeshConfig();

  //Left for our Unity plugin: +Y +X -Z
  auto leftConfig = createMeshNodeConfig(Hermes::Protocol::coor_axis_t::CoorAxisYpos, Hermes::Protocol::coor_axis_t::CoorAxisXpos, Hermes::Protocol::coor_axis_t::CoorAxisZneg);
  meshConfig->set_allocated_leftwrist(leftConfig);
  meshConfig->set_allocated_leftthumb(leftConfig);
  meshConfig->set_allocated_leftfinger(leftConfig);

  //Right for our Unity plugin:: -Y -X -Z
  auto rightConfig = createMeshNodeConfig(Hermes::Protocol::coor_axis_t::CoorAxisYneg, Hermes::Protocol::coor_axis_t::CoorAxisXneg, Hermes::Protocol::coor_axis_t::CoorAxisZneg);
  meshConfig->set_allocated_rightwrist(rightConfig);
  meshConfig->set_allocated_rightthumb(rightConfig);
  meshConfig->set_allocated_rightfinger(rightConfig);

  //World for Unity: +Y +Z +X
  auto worldConfig = createMeshNodeConfig(Hermes::Protocol::coor_axis_t::CoorAxisYpos, Hermes::Protocol::coor_axis_t::CoorAxisZpos, Hermes::Protocol::coor_axis_t::CoorAxisXpos);
  meshConfig->set_allocated_world(worldConfig);

  meshConfig->set_negateaxisx(true);
  meshConfig->set_negateaxisy(false);
  meshConfig->set_negateaxisz(false);

  size_t size = meshConfig->ByteSizeLong();
  meshConfig->SerializeToString(_bytes);
}

Hermes::Protocol::MeshNodeConfig* PrimeIIDriver::createMeshNodeConfig(Hermes::Protocol::coor_axis_t _up, Hermes::Protocol::coor_axis_t _forward, Hermes::Protocol::coor_axis_t _right)
{
  auto meshNodeConfig = new Hermes::Protocol::MeshNodeConfig();
  meshNodeConfig->set_updirection(_up);
  meshNodeConfig->set_forwarddirection(_forward);
  meshNodeConfig->set_rightdirection(_right);
  return meshNodeConfig;
}
