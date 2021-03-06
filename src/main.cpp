//
#include <stdio.h>
#include <iostream>
#include <winsock2.h>
#include <WS2tcpip.h>

#include "../include/PrimeIIDriver.h"
#include "../depend/CJsonObject/CJsonObject.hpp"

#pragma comment(lib,"ws2_32.lib")

using namespace neb;

bool getKeyboard(int key)
{
  return (GetAsyncKeyState(key) & 0x8000);
}

CJsonObject GlovesData2Json(std::vector<PrimeIIDriver::GloveData> gloves)
{
  CJsonObject  glovesJson;
  if (gloves.size())
  {
    for (size_t i = 0; i < gloves.size(); i++)
    {
      CJsonObject gloveJson;
      gloveJson.Add("deviceid", gloves.at(i).deviceid);
      gloveJson.Add("dongleid", gloves.at(i).dongleid);
      gloveJson.Add("handtype", gloves.at(i).handtype);

      CJsonObject wristIMU;
      wristIMU.Add("x", gloves.at(i).wristIMU.x);
      wristIMU.Add("y", gloves.at(i).wristIMU.y);
      wristIMU.Add("z", gloves.at(i).wristIMU.z);
      wristIMU.Add("w", gloves.at(i).wristIMU.w);


      CJsonObject fingersFlex;
      for (size_t j = 0; j < gloves.at(i).fingers.fingersFlex.size(); j++)
      {
        CJsonObject fingerFlex;
        fingerFlex.Add("Joint1Spread", gloves.at(i).fingers.fingersFlex[j].Joint1Spread);
        fingerFlex.Add("Joint1Stretch", gloves.at(i).fingers.fingersFlex[j].Joint1Stretch);
        fingerFlex.Add("Joint2Stretch", gloves.at(i).fingers.fingersFlex[j].Joint2Stretch);
        fingerFlex.Add("Joint3Stretch", gloves.at(i).fingers.fingersFlex[j].Joint3Stretch);

        fingersFlex.Add(fingerNames[j], fingerFlex);
      }
      CJsonObject fingersIMU;
      for (size_t j = 0; j < gloves.at(i).fingers.fingersIMU.size(); j++)
      {
        CJsonObject fingerIMU;
        fingerIMU.Add("x", gloves.at(i).fingers.fingersIMU[j].x);
        fingerIMU.Add("y", gloves.at(i).fingers.fingersIMU[j].y);
        fingerIMU.Add("z", gloves.at(i).fingers.fingersIMU[j].z);
        fingerIMU.Add("w", gloves.at(i).fingers.fingersIMU[j].w);

        fingersIMU.Add(fingerNames[j], fingerIMU);
      }

      CJsonObject fingers;
      fingers.Add("fingersFlex", fingersFlex);
      fingers.Add("fingersIMU", fingersIMU);

      gloveJson.Add("wristIMU", wristIMU);
      gloveJson.Add("fingers", fingers);

      std::stringstream stream;
      stream << "glove" << i + 1;

      glovesJson.Add(stream.str(), gloveJson);
    }
  }

  return glovesJson;
}

int main(int argc, char* argv[])
{
  //init PrimeIIDriver
  PrimeIIDriver pd("PrimeIIDriver", "C++ PrimeIIDriver");
  pd.start();

  //init udp
  WORD sockVersion = MAKEWORD(2, 2);
  WSADATA WSAData;
  if (WSAStartup(sockVersion, &WSAData) != 0)
  {
    std::cout << "init faild!!" << std::endl;
    exit(-1);
  }
  SOCKET sockServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sockServer == INVALID_SOCKET)
  {
    std::cout << "Failed socket()" << std::endl;
    exit(-1);
  }
  unsigned long ul = 1;
  int r = ioctlsocket(sockServer, FIONBIO, &ul);
  if (r == SOCKET_ERROR)
  {
    std::cout << "Failed ioctlsocket()" << std::endl;
    exit(-1);
  }
  //bind port
  sockaddr_in sin;
  sin.sin_family = AF_INET;
  sin.sin_port = htons(10086);
  sin.sin_addr.S_un.S_addr = INADDR_ANY;
  if (bind(sockServer, (LPSOCKADDR)&sin, sizeof(sin)) == SOCKET_ERROR)
  {
    std::cout << "bind error !" << std::endl;
    exit(-1);
  }
  //start listen
  if (listen(sockServer, 5) == SOCKET_ERROR)
  {
    std::cout << "listen error !" << std::endl;
    exit(-1);
  }

  SOCKET sClient;
  sockaddr_in remoteAddr;
  int nAddrlen = sizeof(remoteAddr);

  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  //loop
  char recvBuf[1024] = { 0, };
  while (!getKeyboard(VK_ESCAPE))
  {
    std::cout << "waiting for connection..." << std::endl;
    sClient = accept(sockServer, (SOCKADDR*)&remoteAddr, &nAddrlen);
    if (sClient == SOCKET_ERROR)
    {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        continue;
      }
      else
      {
        std::cout << "unknown error!!" << std::endl;
        break;
      }
    }
    else
    {
      char ipBuf[20] = { '\0' };
      inet_ntop(AF_INET, (void*)&remoteAddr.sin_addr, ipBuf, 16);
      std::cout << "received a connection:" << ipBuf << std::endl;
      std::cout << "start send message" << std::endl;
    }

    while (!getKeyboard(VK_ESCAPE))
    {
      //send message
      std::vector<PrimeIIDriver::GloveData> gloves = pd.getGlovesData();
      CJsonObject glovesJson = GlovesData2Json(gloves);
      if (!glovesJson.GetErrMsg().empty())
      {
        std::cout << "json decode error:" << glovesJson.GetErrMsg() << std::endl;
        continue;
      }
      if (!glovesJson.IsEmpty())
      {
        //std::cout << glovesJson.ToString() << std::endl;

        int sel = send(sClient, glovesJson.ToString().c_str(), glovesJson.ToString().length(), 0);

        if (sel < 0)
        {
          std::cout << "disconnect..." << std::endl;
          break;
        }
      }

      //receive message
      int ret = recv(sClient, recvBuf, 1024, 0);
      if (ret == SOCKET_ERROR)
      {
        int r = WSAGetLastError();
        if (r == WSAEWOULDBLOCK)
        {
          //std::cout << "no message..." << std::endl;
        }
        else if (r == WSAENETDOWN)
        {
          std::cout << "send error..." << std::endl;
        }
      }
      else if (ret > 0)//success
      {
        std::string str(recvBuf);
        CJsonObject json(str);
        if (!json.GetErrMsg().empty())
        {
          std::cout << "json recode error:" << json.GetErrMsg() << std::endl;
        }
        else
        {
          if (!json.IsEmpty())
          {
            uint32_t dongleId = 0;
            int jhandtype = 0;
            PrimeIIDriver::HandType handtype = PrimeIIDriver::HandType::Unknown;
            std::array<float, 5> powers{};

            CJsonObject powerArray;

            if (json.Get("dongleid", dongleId) && json.Get("handtype", jhandtype) && json.Get("power", powerArray))
            {
              switch (jhandtype)
              {
              case 1:
                handtype = PrimeIIDriver::HandType::LeftHand;
                break;
              case 2:
                handtype = PrimeIIDriver::HandType::RightHand;
                break;
              default:
                handtype = PrimeIIDriver::HandType::Unknown;
                break;
              }

              if (powerArray.IsArray() && powerArray.GetArraySize() == 5)
              {
                for (size_t i = 0; i < 5; i++)
                {
                  powerArray.Get(i, powers.at(i));
                }

                pd.setVibrateFingers(dongleId, handtype, powers);
              }
            }
            else
            {
              std::cout << "recv json key error!!!" << std::endl;
            }
          }
        }
      }
      else if (ret == 0)
      {
        std::cout << "disconnect..." << std::endl;
        break;
      }
      else
      {
        std::cout << "recv failed:" << WSAGetLastError() << std::endl;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    closesocket(sClient);
  }

  closesocket(sockServer);
  WSACleanup();

  pd.stop();

  return 0;
}
