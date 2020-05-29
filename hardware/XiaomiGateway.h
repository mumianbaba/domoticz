#pragma once

#include <stdint.h>
#include "DomoticzHardware.h"
#include <boost/tuple/tuple.hpp>
#include <boost/asio.hpp>
#include <list>
#include <mutex>
#include <map>
#include <memory>
#include <iostream>
#include <string>

#include "Xiaomi/DevAttr.hpp"
#include "Xiaomi/Device.hpp"


namespace XiaoMi{


typedef std::map <std::string, DevAttr*> AttrMap;
typedef std::map <std::string, std::shared_ptr<Device>> DeviceMap;




#define MAX_LOG_LINE_LENGTH (2048*3)


class XiaomiGateway : public CDomoticzHardwareBase
{
public:
	explicit XiaomiGateway(const int ID);
	~XiaomiGateway(void);

	/* DT system function */
	bool WriteToHardware(const char *pdata, const unsigned char length) override;
	bool StartHardware() override;
	bool StopHardware() override;
	void Do_Work();
	static void whois(void);

public:
	static void initDeviceAttrMap(const DevInfo devInfo[], int size);
	static const DevAttr* findDevAttr(const std::string& model);
	static bool checkZigbeeMac(const std::string& mac);
	void addDeviceToMap(const std::string& mac, std::shared_ptr<Device> ptr);
	void delDeviceFromMap(const std::string& mac);
	std::shared_ptr<Device> getDevice(const std::string& mac);
	std::shared_ptr<Device> getDevice(unsigned int ssid, int type, int subType, int unit);
	bool createDtDevice(std::shared_ptr<Device> dev);

	bool createWriteParam(const char * pdata, int length, WriteParam& param, std::shared_ptr <Device>& dev);
	bool sendMessageToGateway(const std::string &controlMessage, const std::string ip = "", int port = 0);

	bool isDevInfoInited(){return m_devInfoInited;}
	void setDevInfoInited(bool flag){m_devInfoInited=flag;}
	void joinGatewayHandler(Json::Value& root);
	void deviceListHandler(Json::Value& root);
	void updateHardwareInfo(const std::string& model, const std::string& mac);
	std::string getGatewayIp();
	void setOnlineStatus(std::shared_ptr<Device>& dev, bool status);
	void setOnlineStatus(bool status);
	OnlineStatus getOnlineStatus(std::shared_ptr<Device>& dev);
public:


	void updateSwitch(const std::string &nodeID, const std::string &name, const bool bIsOn, const _eSwitchType switchType, const int unit, const int level, const std::string &messageType,	const int battery);
	void updateRGBLight(const std::string & nodeId, const unsigned char unit, const unsigned char subType,const int onOff, const std::string& brightness, const _tColor&  color, const int battery);

	void updateRGBGateway(const std::string &nodeId, const std::string &name, const bool bIsOn, const int brightness, const int hue);
	void updateCubeText(const std::string &nodeId, const std::string &name, const std::string &degrees);
	void updateVoltage(const std::string &nodeId, const std::string &name, const int voltage);
	void updateLux(const std::string &nodeId, const unsigned char unit, const std::string &name, const int illumination, const int battery);

	void updateTemperature(const std::string &nodeId, const std::string &name, const float temperature, const int battery);
	void updateHumidity(const std::string &nodeId, const std::string &name, const int humidity, const int battery);
	void updatePressure(const std::string &nodeId, const std::string &name, const float pressure, const int battery);
	void updateTempHumPressure(const std::string &nodeId, const std::string &name, const std::string& temperature, const std::string& humidity, const std::string& pressure, const int battery);

	void updateTempHum(const std::string &nodeId, const std::string &name, const std::string& temperature, const std::string&	humidity, const int battery);
	void updateKwh(const std::string & nodeId, const std::string & name, const std::string& loadPower, const std::string& consumed, const int battery);

public:
	void addGatewayToList();
	void rmFromGatewayList();
	static int gatewayListSize();
	static XiaomiGateway* getGatewayByIp(std::string ip);

	int getLocalIpAddr(std::vector<std::string>& ipAddrs);
	std::string getSimilarLocalAddr(const std::string& gwIp);
	std::string getGatewayKey();

public:

	static std::list<XiaomiGateway*> m_gwList;
	static std::mutex m_gwListMutex;
	static std::shared_ptr<boost::thread> m_whoIsThread;


private:

	bool m_outputMessage;
	std::string m_gwSid;
	std::string m_gwModel;
	std::string m_gwIp;
	int m_gwUnicastPort;
	std::string m_localIp;
	std::string m_gwPassword;
	std::shared_ptr<std::thread> m_thread;

	bool m_devInfoInited;
	DeviceMap m_deviceMap;
	static AttrMap m_attrMap;

};

class UdpServer
{
public:
	UdpServer(const std::string &localIp);
	~UdpServer();
	bool initServer();

public:
	static UdpServer* getUdpServer()
	{
		return m_updServer;
	}
	static void setUdpServer(UdpServer* server)
	{
		m_updServer = server;
	}
	static void resetUdpServer()
	{
		m_updServer = nullptr;
	}

	bool run();
	void stop();

private:

	void startReceive();
	bool recvParamCheck(Json::Value& root);
	void handleReceive(const boost::system::error_code& error, std::size_t /*bytes_transferred*/);

private:
	boost::asio::io_service m_ioService;
	boost::asio::ip::udp::socket m_socket;
	boost::asio::ip::udp::endpoint m_remoteEndpoint;
	enum { max_length = 8192 };
	char data_[max_length];
	std::string m_gwIp;
	std::string m_localIp;
	boost::thread  m_serThread;

public:
	static std::mutex m_mutex;
	static volatile bool m_isRun;
	static UdpServer* m_updServer;

};

class TokenManager {
public:
	static TokenManager& getInstance();
	void updateTokenSid(const std::string &ip, const std::string &token, const std::string &sid);
	std::string getToken(const std::string &ip);
	std::string getSid(const std::string &sid);
private:
	std::mutex m_mutex;
	std::vector<boost::tuple<std::string, std::string, std::string> > m_gwTokens;

	TokenManager()
	{
		std::cout<<"TokenManager"<<std::endl;
	}
	~TokenManager()
	{
		std::cout<<"~TokenManager"<<std::endl;
	}

};


}
