#pragma once

#include <stdint.h>
#include "DomoticzHardware.h"
#include <boost/tuple/tuple.hpp>
#include <list>
#include <mutex>
#include <map>
#include <memory>
#include <iostream>
#include <string>

#include "Xiaomi/Outlet.hpp"
#include "Xiaomi/DevAttr.hpp"
#include "Xiaomi/Device.hpp"


namespace XiaoMi{


typedef std::map <std::string, DevAttr*> AttrMap;
typedef std::map <std::string, std::shared_ptr<Device>> DeviceMap;




#define MAX_LOG_LINE_LENGTH (2048*3)

#define SP_MODLE  		0  //string
#define SP_NAME   		1  //string
#define SP_TYPE   		2  //int
#define SP_SUBTYPE   	3  //int
#define SP_SWTYPE   	4  //int
#define SP_UINT   		5  //int
#define SP_Outlet   	6  //int

#define SP_OPT   		7  //string


#define DY_SSID   		0
#define DY_MODLE  		1
#define DY_MAC   	 	2
#define DY_DEVICEID  	3



class XiaomiGateway : public CDomoticzHardwareBase
{
public:
	explicit XiaomiGateway(const int ID);
	~XiaomiGateway(void);
	bool WriteToHardware(const char *pdata, const unsigned char length) override;

	int	WriteToGeneralSwitch(const tRBUF *pCmd, int length,Json::Value& json);
	int WriteToColorSwitch(const tRBUF *pCmd, int length, Json::Value& json);
	int WriteToMannageDeive(const tRBUF *pCmd, int length, Json::Value& json);

	int GetGatewayHardwareID(){ return m_HwdID; };
	std::string GetGatewayIp(){ return m_GatewayIp; };
	std::string GetGatewaySid(){ if (m_GatewaySID == "") m_GatewaySID = XiaomiGatewayTokenManager::GetInstance().GetSID(m_GatewayIp); return m_GatewaySID; };

	bool IsMainGateway(){ return m_ListenPort9898; };
	void SetAsMainGateway(){ m_ListenPort9898 = true; };
	void UnSetMainGateway(){ m_ListenPort9898 = false; };


	void RegisterSupportDevice(const std::string & model, const std::string & name,
											const int type, const int subtype, const int swtype, const int uint,
											const int outlet, const std::string & devopt);
	int GetUincastPort();
	void SetUincastPort(int port);

	std::vector<std::vector<std::string>>  GetDeviceInfoByModel(const std::string & model);

	int          GetSsidBySid(const int devType, const int subType, const int sID);
	int          GetSsidBySid(const int devType, const int subType, const std::string& sid);
	std::string  GetDeviceIdBySsid(const int devType, const int subType, unsigned int rowId);
	unsigned int GetSsidByDeviceId(const int devType, const int subType, const std::string& deviceID);
	bool 		 SetDeviceInfo(const std::string & mac, const std::string & model);
	void 		 DelDeviceInfo(const int ssid);

	std::string  GetDeviceMac(const int ssid);
	std::string  GetDeviceModel(const int ssid);
	std::string  GetDeviceModel(const std::string& mac);
	int 		 GetDeviceSsid(const std::string& mac);
	std::string  GetDeviceId(const std::string& mac);
	std::string  GetDeviceId(const int ssid);
	bool 		 IsDevInfoInited(){return m_bDevInfoInited;}
	void 		 SetDevInfoInited(bool flag){m_bDevInfoInited=flag;}


	bool createDtDevice(std::shared_ptr<Device> dev);


	static void initDeviceAttrMap(const DevInfo devInfo[], int size);
	static void addDeviceToMap(std::string& mac, std::shared_ptr<Device> ptr);
	static void delDeviceFromMap(std::string& mac);
	static const DevAttr* findDevAttr(std::string& model);

	static std::shared_ptr<Device> getDevice(std::string& mac);
	static std::shared_ptr<Device> getDevice(unsigned int ssid, int type, int subType, int unit);

public:
	static std::vector<boost::tuple<std::string, std::string, int, int, int, int, int, std::string> >  m_SpDevice;
private:
	bool m_bDevInfoInited;
	static AttrMap m_attrMap;
	static DeviceMap m_deviceMap;


public:
	bool StartHardware() override;
	bool StopHardware() override;
	void Do_Work();

	bool SendMessageToGateway(const std::string &controlmessage);
	void InsertUpdateSwitch(const std::string &nodeid, const std::string &Name, const bool bIsOn, const _eSwitchType switchtype, const int unittype, const int level, const std::string &messagetype, const std::string &load_power, const std::string &power_consumed, const int battery);
	void InsertUpdateRGBLight(const std::string & nodeid, const std::string & Name, const unsigned char SubType, const unsigned char Mode, const std::string& Color, const std::string& Brightness, const bool bIsWhite,  const int battery);

	void InsertUpdateRGBGateway(const std::string &nodeid, const std::string &Name, const bool bIsOn, const int brightness, const int hue);
	void InsertUpdateCubeText(const std::string &nodeid, const std::string &Name, const std::string &degrees);
	void InsertUpdateVoltage(const std::string &nodeid, const std::string &Name, const int VoltageLevel);
	void InsertUpdateLux(const std::string &nodeid, const std::string &Name, const int Illumination, const int battery);

	void InsertUpdateTemperature(const std::string &nodeid, const std::string &Name, const float Temperature, const int battery);
	void InsertUpdateHumidity(const std::string &nodeid, const std::string &Name, const int Humidity, const int battery);
	void InsertUpdatePressure(const std::string &nodeid, const std::string &Name, const float Pressure, const int battery);
	void InsertUpdateTempHumPressure(const std::string &nodeid, const std::string &Name, const std::string& Temperature, const std::string& Humidity, const std::string& Pressure, const int battery);

	void InsertUpdateTempHum(const std::string &nodeid, const std::string &Name, const std::string& Temperature, const std::string&	Humidity, const int battery);
	void InsertUpdateKwh(const std::string & nodeid, const std::string & Name, const std::string& LoadPower, const std::string& Consumed, const int battery);

	std::string GetGatewayKey();
	unsigned int GetShortID(const std::string & nodeid);


	std::vector<boost::tuple<int, std::string, std::string, std::string> > m_DevInfo;
	bool m_bDoRestart;
	std::shared_ptr<std::thread> m_thread;
	std::shared_ptr<std::thread> m_udp_thread;
	bool m_OutputMessage;
	bool m_IncludeVoltage;
	bool m_ListenPort9898;
	uint8_t m_GatewayRgbR;          //TODO: Remove, otherwise colors will be mixed up if controlling more than one bulb
	uint8_t m_GatewayRgbG;          //TODO: Remove, otherwise colors will be mixed up if controlling more than one bulb
	uint8_t m_GatewayRgbB;          //TODO: Remove, otherwise colors will be mixed up if controlling more than one bulb
	uint8_t m_GatewayRgbCW;          //TODO: Remove, otherwise colors will be mixed up if controlling more than one bulb
	uint8_t m_GatewayRgbWW;          //TODO: Remove, otherwise colors will be mixed up if controlling more than one bulb
	uint8_t m_GatewayRgbCT;          //TODO: Remove, otherwise colors will be mixed up if controlling more than one bulb
	uint8_t m_GatewayBrightnessInt; //TODO: Remove, otherwise colors will be mixed up if controlling more than one bulb
	std::string m_GatewaySID;
	std::string m_GatewayIp;
	int m_GatewayUPort;
	std::string m_LocalIp;
	std::string m_GatewayPassword;
	std::string m_GatewayMusicId;
	std::string m_GatewayVolume;
	std::mutex m_mutex;
	
	XiaomiGateway * GatewayByIp( std::string ip );
	void AddGatewayToList();
	void RemoveFromGatewayList();

	int get_local_ipaddr(std::vector<std::string>& ip_addrs);
	
	class xiaomi_udp_server
	{
	public:
		xiaomi_udp_server(boost::asio::io_service & io_service, int m_HwdID, const std::string &gatewayIp, const std::string &localIp, const bool listenPort9898, const bool outputMessage, const bool includeVolage, XiaomiGateway *parent);
		~xiaomi_udp_server();
		void whois(void);

	private:

		void start_receive();
		void handle_receive(const boost::system::error_code& error, std::size_t /*bytes_transferred*/);

		boost::asio::ip::udp::socket socket_;
		boost::asio::ip::udp::endpoint remote_endpoint_;
		enum { max_length = 8192 };
		char data_[max_length];
		int m_HardwareID;
		std::string m_gatewayip;
		int m_uincastport;
		std::string m_localip;
		bool m_OutputMessage;
		bool m_IncludeVoltage;
		XiaomiGateway* m_XiaomiGateway;
	};

	class XiaomiGatewayTokenManager {
	public:
		static XiaomiGateway::XiaomiGatewayTokenManager& GetInstance();
		void UpdateTokenSID(const std::string &ip, const std::string &token, const std::string &sid);
		std::string GetToken(const std::string &ip);
		std::string GetSID(const std::string &sid);
	private:
		std::mutex m_mutex;
		std::vector<boost::tuple<std::string, std::string, std::string> > m_GatewayTokens;

		XiaomiGatewayTokenManager() { ; }
		~XiaomiGatewayTokenManager() { ; }
	};
};

}
