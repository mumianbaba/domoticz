#include "stdafx.h"
#include "../hardware/hardwaretypes.h"
#include "../main/localtime_r.h"
#include "../main/Logger.h"
#include "../main/mainworker.h"
#include "../main/Helper.h"
#include "../main/SQLHelper.h"
#include "../main/WebServer.h"
#include "../webserver/cWebem.h"
#include "../main/json_helper.h"
#include "../main/RFXtrx.h"
#include "XiaomiGateway.h"
#include <openssl/aes.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/system/system_error.hpp>
#include <inttypes.h>
#include <memory>
#include <exception>
#include <stdexcept>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "Xiaomi/Outlet.hpp"
#include "Xiaomi/DevAttr.hpp"
#include "Xiaomi/Device.hpp"
#include "Xiaomi/SupportDevice.hpp"

#ifndef WIN32
#include <ifaddrs.h>
#endif




#define round(a) ( int ) ( a + .5 )


namespace XiaoMi{

const char* keyCmd = "cmd";
const char* keyModel = "model";
const char* keySid = "sid";
const char* keyParams = "params";
const char* keyToken = "token";
const char* keyDevList ="dev_list";
const char* keyIp =	"ip";
const char* keyProtocol ="protocal";
const char* keyPort ="port";
const char* keyKey = "key";

const char* keyBattery = "battery_voltage";

const char* cmdWhoIs = "whois";
const char* rspWhoIs = "iam";

const char* cmdDiscorey = "discovery";
const char* rspDiscorey = "discovery_rsp";

const char* cmdRead = "read";
const char* rspRead = "read_rsp";

const char* cmdWrite = "write";
const char* rspWrite = "write_rsp";

const char* repReport = "report";
const char* repHBeat = "heartbeat";



}


using namespace XiaoMi;

/* Gateway online list and mutex */
std::list<XiaomiGateway*> XiaomiGateway::m_gwList;
std::mutex XiaomiGateway::m_gwListMutex;


/* support device list and online device list */
AttrMap XiaomiGateway::m_attrMap;
std::shared_ptr<boost::thread> XiaomiGateway::m_whoIsThread;

std::shared_ptr<UdpServer> UdpServer::m_updServer;
std::shared_ptr< boost::asio::io_service> UdpServer::m_ioService;

bool XiaomiGateway::checkZigbeeMac(const std::string& mac)
{
	if (mac.size() != 16){
		_log.Log(LOG_ERROR, "zigbee mac size not equal 16");
		return false;
	}

	int ii = 0;
	for (const auto& itt : mac){

		if ((itt >= '0' &&  itt <= '9') ||
		(itt >= 'a' && itt <= 'f') ||
		(itt >= 'A' && itt <= 'F')){
			ii++;
		}
	}
	return (ii == 16);
}


XiaomiGateway::XiaomiGateway(const int ID)
{
	m_HwdID = ID;
	m_devInfoInited = false;
	m_gwUnicastPort = 0;
	if (m_attrMap.empty()){
		initDeviceAttrMap(devInfoTab, sizeof(devInfoTab)/sizeof(devInfoTab[0]));
	}
}

XiaomiGateway::~XiaomiGateway(void)
{
	std::cout<<"~XiaomiGateway"<<std::endl;
	auto server = UdpServer::getUdpServer();
	if (gatewayListSize() == 0 && server){
		server->stop();
		_log.Log(LOG_STATUS, "udp service is stop");
	}

	if (gatewayListSize() == 0 && m_whoIsThread){
		m_whoIsThread->interrupt();
		m_whoIsThread->join();
		m_whoIsThread.reset();
		_log.Log(LOG_STATUS, "whois service is stop");
	}
}

bool XiaomiGateway::StartHardware()
{
	RequestStart();
	m_devInfoInited = false;
	auto result =  m_sql.safe_query
		("SELECT Password, Address FROM Hardware WHERE Type=%d AND ID=%d AND Enabled=1",
		  HTYPE_XiaomiGateway, m_HwdID);

	if (result.empty()){
		_log.Log(LOG_ERROR, "Not find or not enable the hardware(type:%d, id=%d)", HTYPE_XiaomiGateway, m_HwdID);
		return false;
	}

	m_gwPassword = result[0][0].c_str();
	m_gwIp = result[0][1].c_str();
	m_outputMessage = false;
	result = m_sql.safe_query("SELECT Value FROM UserVariables WHERE (name == 'XiaomiMessage')");
	if (!result.empty()){
		m_outputMessage = true;
	}
	TokenManager::getInstance();
	addGatewayToList();

	m_thread = std::shared_ptr<std::thread>(new std::thread(&XiaomiGateway::Do_Work, this));
	SetThreadNameInt(m_thread->native_handle());
	return (m_thread != nullptr);
}

bool XiaomiGateway::StopHardware()
{
	if (m_thread){
		RequestStop();
		m_thread->join();
		m_thread.reset();
	}
	m_devInfoInited = false;
	m_deviceMap.clear();
	rmFromGatewayList();
	m_gwSid = "";
	m_gwModel = "";
	m_gwIp = "";
	m_gwUnicastPort = 0;
	m_localIp = "";
	m_gwPassword = "";
	return true;
}


int XiaomiGateway::getLocalIpAddr(std::vector<std::string>& ipAddrs)
{
#ifdef WIN32
	return 0;
#else
	struct ifaddrs *myaddrs, *ifa;
	void *in_addr;
	char buf[64];
	int count = 0;

	if (getifaddrs(&myaddrs) != 0){
		_log.Log(LOG_ERROR, "getifaddrs failed! (when trying to determine local ip address)");
		perror("getifaddrs");
		return 0;
	}

	for (ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next){
		if (ifa->ifa_addr == NULL  || !(ifa->ifa_flags & IFF_UP)){
			continue;
		}

		switch (ifa->ifa_addr->sa_family)
		{
			case AF_INET:
			{
				struct sockaddr_in *s4 = (struct sockaddr_in *)ifa->ifa_addr;
				in_addr = &s4->sin_addr;
				break;
			}

			case AF_INET6:
			{
				struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)ifa->ifa_addr;
				in_addr = &s6->sin6_addr;
				break;
			}

			default:
				continue;
		}

		if (!inet_ntop(ifa->ifa_addr->sa_family, in_addr, buf, sizeof(buf))){
			_log.Log(LOG_ERROR, "Could not convert to IP address, inet_ntop failed for interface %s", ifa->ifa_name);
		}
		else{
			ipAddrs.push_back(buf);
			count++;
		}
	}

	freeifaddrs(myaddrs);
	return count;
#endif
}



std::string XiaomiGateway::getSimilarLocalAddr(const std::string& gwIp)
{
	std::string ip;
	try{
		/* There is a deep meaning in this, to distinguish has forgotten */
		std::string compareIp = gwIp.substr(0, (gwIp.length() - 3));
		std::vector<std::string> ipAddrs;
		if (getLocalIpAddr(ipAddrs) > 0){

			for (const std::string &addr : ipAddrs){
				std::size_t found = addr.find(compareIp);
				if (found != std::string::npos){
					ip = addr;
					_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Using %s for local IP address.",
							m_HwdID, ip.c_str());
					break;
				}
			}
		}
		else
		{
			_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Could not find local IP address with ifaddrs", m_HwdID);
		}
	}

	catch (std::exception& e){
		_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Could not find local IP address with ifaddrs: %s", m_HwdID, e.what());
	}

	return ip;
}
void XiaomiGateway::Do_Work()
{
	_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): Worker started...", m_HwdID);
	m_localIp = getSimilarLocalAddr(m_gwIp);
	if (!UdpServer::getUdpServer()){

		auto ioService = std::make_shared<boost::asio::io_service>();
		auto server = std::make_shared<UdpServer>(ioService, m_localIp);

		bool res = server->initServer();
		if (false == res){
			StopHardware();
			_log.Log(LOG_ERROR, "init upd server failed");
			return;
		}

		res = server->run();
		if (false == res){
			StopHardware();
			_log.Log(LOG_ERROR, "init upd server failed");
			return;
		}
		UdpServer::setUdpServer(server);
		_log.Log(LOG_STATUS, "XiaomiGateway: udp server is start...");
	}
	sendMessageToGateway("{\"cmd\":\"whois\"}", "224.0.0.50", 4321);


	if (!m_whoIsThread){

		m_whoIsThread = std::make_shared<boost::thread>(XiaomiGateway::whois);
		SetThreadName(m_whoIsThread->native_handle(), "XiaomiGwWhois");
		_log.Log(LOG_STATUS, "XiaomiGateway: whois thread  is start...");
	}


	int sec_counter = 0;
	while (!IsStopRequested(1000))
	{
		sec_counter++;
		if (sec_counter % 12 == 0) {
			m_LastHeartbeat = mytime(NULL);
		}
	}
	_log.Log(LOG_STATUS, "XiaomiGateway (ID=%d): stopped", m_HwdID);
}

std::string XiaomiGateway::getGatewayIp()
{
	return m_gwIp;
}


XiaomiGateway * XiaomiGateway::getGatewayByIp(std::string ip)
{
	XiaomiGateway * ret = NULL;
	{
		std::unique_lock<std::mutex> lock(m_gwListMutex);
		for (const auto& itt : m_gwList){
			if (itt->getGatewayIp() == ip){
				_log.Debug(DEBUG_HARDWARE, "XiaomiGateway: find a gateway ip:%s at list", ip.c_str());
				ret = itt;
				break;
			}
		}
	}
	return ret;
}

int XiaomiGateway::gatewayListSize()
{
	int size = 0;
	{
		std::unique_lock<std::mutex> lock(m_gwListMutex);
		size = m_gwList.size();
	}
	return size;
}

void XiaomiGateway::addGatewayToList()
{
	std::unique_lock<std::mutex> lock(m_gwListMutex);
	auto it = std::find(m_gwList.begin(), m_gwList.end(), this);
	if (it == m_gwList.end()){
		m_gwList.push_back(this);
	}
}

void XiaomiGateway::rmFromGatewayList()
{
	std::unique_lock<std::mutex> lock(m_gwListMutex);
	m_gwList.remove(this);
}

void XiaomiGateway::initDeviceAttrMap(const DevInfo devInfo[], int size)
{
	int ii;
	for (ii = 0; ii < size; ii++){
		m_attrMap.emplace(devInfo[ii].zigbeeModel, new DevAttr(devInfo[ii]));
	}

	_log.Log(LOG_STATUS, "support device number: %lu", m_attrMap.size());
	for (const auto & itt : m_attrMap){
		_log.Debug(DEBUG_HARDWARE, "zigbee model: %s", itt.first.c_str());
	}
	return;
}

const DevAttr* XiaomiGateway::findDevAttr(std::string& model)
{
	auto it = m_attrMap.find(model);
	if (it == m_attrMap.end()){
		_log.Log(LOG_STATUS, "error: not support zigbee model: %s", model.c_str());
		return nullptr;
	}
	return it->second;
}

void XiaomiGateway::addDeviceToMap(std::string& mac, std::shared_ptr<Device> ptr)
{
	if (!ptr){
		_log.Log(LOG_ERROR, "add device to map, device null");
		return;
	}
	m_deviceMap.emplace(mac, ptr);
	_log.Debug(DEBUG_HARDWARE, "add device to map mac: %s", mac.c_str());
}

void XiaomiGateway::delDeviceFromMap(std::string& mac)
{
	m_deviceMap.erase(mac);
	_log.Debug(DEBUG_HARDWARE, "delDeviceFromMap mac: %s", mac.c_str());
}

std::shared_ptr<Device> XiaomiGateway::getDevice(std::string& mac)
{
	std::shared_ptr<Device> ptr;
	ptr.reset();
	auto it = m_deviceMap.find(mac);
	if (it == m_deviceMap.end()){
		_log.Log(LOG_STATUS, "error getDevice failed mac:%s", mac.c_str());
		return ptr;
	}
	return it->second;
}

std::shared_ptr<Device> XiaomiGateway::getDevice(unsigned int ssid, int type, int subType, int unit)
{
	bool res;
	std::shared_ptr<Device> ptr;
	ptr.reset();
	for (const auto itt : m_deviceMap){
		res = itt.second->match(ssid, type, subType, unit);
		if (true == res){
			ptr = itt.second;
			break;
		}
	}
	if (!ptr){
		_log.Log(LOG_ERROR, "getDevice failed ssid:%x  type:%x  subType:%x  unit:%d", ssid, type, subType, unit);
	}
	return ptr;
}



bool XiaomiGateway::createWriteParam(const char * pdata, int length, WriteParam& param, std::shared_ptr <Device>& dev)
{
	if (pdata == nullptr){
		_log.Log(LOG_ERROR, "message ptr null");
		return false;
	}
	const tRBUF *pCmd = reinterpret_cast<const tRBUF *>(pdata);
	unsigned char packetType = pCmd->ICMND.packettype;
	unsigned char subType = pCmd->ICMND.subtype;
	unsigned int ssid = 0;
	unsigned char unit = 0;
	switch(packetType)
	{
		case pTypeGeneralSwitch:
		{
			const _tGeneralSwitch *xcmd = reinterpret_cast<const _tGeneralSwitch*>(pCmd);
			ssid = xcmd->id;
			unit = xcmd->unitcode;
		}
		break;
		case pTypeColorSwitch:
		{
			const _tColorSwitch *xcmd = reinterpret_cast<const _tColorSwitch*>(pCmd);
			ssid = xcmd->id;
			unit = xcmd->dunit;
		}
		break;
		case pTypeMannageDevice:
		{
			ssid = TbGateway::idConvert(m_gwSid).first;
			unit = 1;
		}
		break;
		default:
			_log.Log(LOG_ERROR, "no support type on write");
			return false;
		break;
	}

	param.packet = reinterpret_cast<const unsigned char*>(pCmd);
	param.len = length;
	param.type = packetType;
	param.subType = subType;
	param.unit =unit;
	param.gwMac = m_gwSid;
	param.miGateway = static_cast<void*>(this);

	dev.reset();
	dev = getDevice(ssid,
					static_cast<int>(packetType),
					static_cast<int>(subType),
					static_cast<int>(unit));
	if (!dev){
		_log.Log(LOG_ERROR, "no find want to write device");
		return false;
	}

	if (packetType == pTypeMannageDevice){
		const tRBUF *xcmd = reinterpret_cast<const tRBUF *>(pdata);
		if (xcmd->MANNAGE.cmnd != cmdRmDevice){
			return true;
		}
		int rmSsid = xcmd->MANNAGE.id1 << 24 | xcmd->MANNAGE.id2 << 16 |
					xcmd->MANNAGE.id3 << 8 | xcmd->MANNAGE.id4;
		int rmType = xcmd->MANNAGE.value1;
		int rmSubType = xcmd->MANNAGE.value2;
		int rmUnit = xcmd->MANNAGE.value3;

		std::shared_ptr <Device> rmDev = getDevice(rmSsid, rmType, rmSubType, rmUnit);

		if (!rmDev){
			_log.Log(LOG_ERROR, "no find want to delete device");
			return false;
		}
		param.rmMac = rmDev->getMac();
	}
	return true;
}

bool XiaomiGateway::WriteToHardware(const char * pdata, const unsigned char length)
{
	bool result = true;
	WriteParam param;
	std::shared_ptr<Device> dev;

	if (!isDevInfoInited()){
		_log.Log(LOG_ERROR, "DT and zgbd shakehand(get dev list) not finished, please check");
		return false;
	}
	result = createWriteParam(pdata, length, param, dev);
	if(false == result){
		_log.Log(LOG_ERROR, "pdate param check error");
		return false;
	}

	const tRBUF *pCmd = reinterpret_cast<const tRBUF *>(pdata);
	unsigned char packettype = pCmd->ICMND.packettype;

	if (m_gwSid == ""){
		m_gwSid = TokenManager::getInstance().getSid(m_gwIp);
	}

	result = dev->writeTo(param);
	if (false == result){
		sleep_milliseconds(300);
		result = dev->writeTo(param);
	}

	if (result && packettype == pTypeMannageDevice ){

		const tRBUF *xcmd = reinterpret_cast<const tRBUF *>(pdata);
		if (xcmd->MANNAGE.cmnd == cmdRmDevice){
			delDeviceFromMap(param.rmMac);
		}
	}
	return result;
}

bool XiaomiGateway:: sendMessageToGateway(const std::string &controlMessage, const std::string ip, int port) {
	if ((m_gwUnicastPort == 0 && port == 0) || (ip.empty() && m_gwIp.empty())){
		_log.Log(LOG_ERROR, "Please ip or port is null, please check ip amd port");
		return false;
	}

	std::string message = controlMessage;
	std::string remoteIp = (!ip.empty())? ip : m_gwIp;
	int remotePort = (port != 0)? port : m_gwUnicastPort;
	bool result = true;
	boost::asio::io_service io_service;
	boost::asio::ip::udp::socket socket_(io_service, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0));
	stdreplace(message, "@gatewaykey", getGatewayKey());
	std::shared_ptr<std::string> message1(new std::string(message));
	boost::asio::ip::udp::endpoint remote_endpoint_;
	remote_endpoint_ = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(remoteIp), remotePort);
	try{
		socket_.send_to(boost::asio::buffer(*message1), remote_endpoint_);
		sleep_milliseconds(50);
		socket_.close();
	}
	catch (boost::system::system_error &e)
	{
		boost::system::error_code ec = e.code();
		std::cerr << ec.value() << std::endl;
		std::cerr << ec.category().name() << std::endl;
		_log.Log(LOG_ERROR,  "send to GW error");
		return false;
	}

	_log.Log(LOG_STATUS,  "send to GW :%s", message.c_str());
	return result;
}


std::string XiaomiGateway::getGatewayKey()
{
#ifdef WWW_ENABLE_SSL
	const unsigned char *key = (unsigned char *)m_gwPassword.c_str();
	unsigned char iv[AES_BLOCK_SIZE] = { 0x17, 0x99, 0x6d, 0x09, 0x3d, 0x28, 0xdd, 0xb3, 0xba, 0x69, 0x5a, 0x2e, 0x6f, 0x58, 0x56, 0x2e };
	std::string token = TokenManager::getInstance().getToken(m_gwIp);
	unsigned char *plaintext = (unsigned char *)token.c_str();
	unsigned char ciphertext[128];

	AES_KEY encryption_key;
	AES_set_encrypt_key(key, 128, &(encryption_key));
	AES_cbc_encrypt((unsigned char *)plaintext, ciphertext, sizeof(plaintext) * 8, &encryption_key, iv, AES_ENCRYPT);

	char gatewaykey[128];
	for (int i = 0; i < 16; i++){
		sprintf(&gatewaykey[i * 2], "%02X", ciphertext[i]);
	}

#ifdef _DEBUG
	_log.Log(LOG_STATUS, "XiaomiGateway: getGatewayKey Password - %s, token=%s", m_gwPassword.c_str(), token.c_str());
	_log.Log(LOG_STATUS, "XiaomiGateway: getGatewayKey key - %s", gatewaykey);
#endif
	return gatewaykey;
#else
	_log.Log(LOG_ERROR, "XiaomiGateway: getGatewayKey NO SSL AVAILABLE");
	return std::string("");
#endif
}


void XiaomiGateway::updateHardwareInfo(const std::string& model, const std::string& mac)
{
	auto result = m_sql.safe_query("SELECT  Model, Mac FROM Hardware WHERE (ID==%d)", m_HwdID);
	if (result.empty()){
		_log.Log(LOG_ERROR, "not find the hwid:%d at db, so interesting", m_HwdID);
		return;
	}

	std::string oldModel = result[0][0];
	std::string oldMac = result[0][1];
	if (oldModel != "Unknown" && oldMac != "Unknown"){
		return;
	}
	m_sql.safe_query("UPDATE Hardware SET Model='%q', Mac='%q'  WHERE (ID==%d)", model.c_str(), mac.c_str() , m_HwdID);
	_log.Log(LOG_STATUS, "update the hardware model and mac, %s  %s",  model.c_str(),  mac.c_str());
}

void XiaomiGateway::deviceListHandler(Json::Value& root)
{
	std::string gwSid;
	std::string token;
	std::string sid;
	std::string model;
	std::string message;

	gwSid = root[keySid].asString();
	token = root[keyToken].asString();
	m_gwSid = gwSid;
	if (!getDevice(gwSid))
	{
		const DevAttr* attr = XiaomiGateway::findDevAttr(m_gwModel);
		if (attr == nullptr)
		{
			_log.Log(LOG_ERROR, "XiaomiGateway: No support the gateway(%s, %s)", m_gwModel.c_str(), gwSid.c_str());
			return;
		}

		std::shared_ptr<Device> dev(new Device (gwSid, attr));
		XiaomiGateway::addDeviceToMap(gwSid, dev);
		updateHardwareInfo(m_gwModel, gwSid);
	}
	TokenManager::getInstance().updateTokenSid(m_gwIp, token, gwSid);
	setDevInfoInited(true);

	Json::Value list = root[keyDevList];
	for (int ii = 0; ii < (int)list.size(); ii++)
	{

		if (false == list[ii].isMember(keySid) ||
			false == list[ii].isMember(keyModel) ||
			false == list[ii][keySid].isString() ||
			false == list[ii][keyModel].isString())
		{
			_log.Log(LOG_ERROR, "XiaomiGateway: dev_list sid or model not in json");
			continue;
		}

		sid = list[ii][keySid].asString();
		model = list[ii][keyModel].asString();

		if (sid.empty() || model.empty() || !checkZigbeeMac(sid))
		{
			_log.Log(LOG_ERROR, "XiaomiGateway: dev_list sid or model empty or mac invalid");
			continue;
		}
		/* if device support */
		const DevAttr* attr = XiaomiGateway::findDevAttr(model);
		if(nullptr == attr)
		{
			Json::Value del_cmd;
			del_cmd[keyCmd] = "write";
			del_cmd[keyKey] = "@gatewaykey";
			del_cmd[keySid] = gwSid;
			del_cmd[keyParams][0]["remove_device"] = sid;
			message = JSonToRawString (del_cmd);
			std::shared_ptr<std::string> send_buff(new std::string(message));
			sendMessageToGateway(message);
			_log.Log(LOG_ERROR, "XiaomiGateway: not support the device model:%s  mac:%s", sid.c_str(), model.c_str());
			continue;
		}
		/* if device not exist */
		if (!getDevice(sid))
		{
			std::shared_ptr<Device> dev(new Device (sid, attr));
			XiaomiGateway::addDeviceToMap(sid, dev);
			createDtDevice(dev);
		}

		Json::Value read_cmd;
		read_cmd[keyCmd] = "read";
		read_cmd[keySid] = sid;
		message = JSonToRawString (read_cmd);
		sendMessageToGateway(message);
	}

}

void XiaomiGateway::joinGatewayHandler(Json::Value& root)
{
	std::string model = root["model"].asString();
	std::string ip = root["ip"].asString();
	std::string port =	root["port"].asString();

	if (ip != getGatewayIp())
	{
		_log.Log(LOG_ERROR, "a new gateway incoming, but not in domoticz hardware list, ip:%s", ip.c_str());
		return;
	}

	m_gwUnicastPort = std::stoi(port);
	m_gwModel = model;

	const DevAttr* attr = XiaomiGateway::findDevAttr(m_gwModel);
	if(attr == nullptr)
	{
		_log.Log(LOG_ERROR, "No support Gateway model:%s", model.c_str());
		return;
	}

	std::string message = "{\"cmd\" : \"discovery\"}";
	sendMessageToGateway(message);
	_log.Log(LOG_STATUS, "Tenbat Gateway(%s) Detected", model.c_str());
}

void XiaomiGateway::whois(void)
{
	while (1)
	{
		boost::this_thread::sleep(boost::posix_time::seconds(10));

		XiaomiGateway* firstGw = NULL;
		{
			std::unique_lock<std::mutex> lock(m_gwListMutex);
			auto it = m_gwList.begin();
			firstGw = *it;
			if (firstGw && !firstGw->isDevInfoInited())
			{
				std::string message = "{\"cmd\":\"whois\"}";
				firstGw->sendMessageToGateway(message, "224.0.0.50", 4321);
				_log.Log(LOG_ERROR, "send whois to udp group port 4321");
			}
		}
	}
}


static inline int customImage(int switchType)
{
	int customimage = 0;
	if (customimage == 0)
	{
		if (switchType == static_cast<int>(STYPE_OnOff))
		{
			customimage = 1; //wall socket
		}
		else if (switchType == static_cast<int>(STYPE_Selector))
		{
			customimage = 9;
		}
	}
	return customimage;
}

bool XiaomiGateway::createDtDevice(std::shared_ptr<Device> dev)
{
	if(!dev)
	{
		_log.Log(LOG_ERROR, "create Dt Device param dev null");
		return false;
	}

	int ii;
	int type, subType, switchType, uint, outlet;
	unsigned int ssid;
	uint64_t DeviceRowIdx = (uint64_t)-1;

	std::string name = dev->getName();
	std::string model = dev->getZigbeeModel();
	std::string mac = dev->getMac();
	auto outletAttr = dev->getOutlet();
	auto ssidPair =  dev->getSsid();

	std::string devID = "";
	std::string devOpt = "";
	std::string opts = "";

	if (ssidPair.size() != outletAttr.size())
	{
		_log.Log(LOG_ERROR,  "outlet number(%lu) not equal ssid number(%lu)", ssidPair.size(), outletAttr.size());
		return false;
	}

	std::cout<<name<<std::endl;
	std::cout<<model<<std::endl;
	std::cout<<mac<<std::endl;

	ii = 0;
	for (const auto itt : outletAttr)
	{
		type = itt->getType();
		subType = itt->getSubType();
		switchType = itt->getSWType();
		uint = itt->getUnit();
		outlet = ii;
		devOpt = itt->getOpts();
		ssid = ssidPair[ii].first;
		devID = ssidPair[ii].second;
		ii++;
		if(m_sql.DoesDeviceExist(m_HwdID, devID.c_str(), uint, type, subType) == true)
		{
			continue;
		}
		bool bPrevAcceptNewHardware = m_sql.m_bAcceptNewHardware;
		m_sql.m_bAcceptNewHardware = true;
		DeviceRowIdx = m_sql.CreateDevice(m_HwdID, type, subType, name, ssid, opts, uint);
		m_sql.m_bAcceptNewHardware = bPrevAcceptNewHardware;

		if (DeviceRowIdx == (uint64_t)-1)
		{
			_log.Log(LOG_ERROR,  "add new device failed model:%s, ssid:%08X", model.c_str(), ssid);
			continue;
		}
		int cImage = customImage(switchType);
		m_sql.safe_query("UPDATE DeviceStatus SET Outlet=%d, SwitchType=%d, CustomImage=%i, Model='%q', Mac='%q' WHERE (HardwareID == %d) AND (ID == %" PRIu64 ")", outlet, switchType, cImage,  model.c_str(), mac.c_str(), m_HwdID, DeviceRowIdx);
		if (devOpt != "")
		{
			m_sql.SetDeviceOptions(DeviceRowIdx, m_sql.BuildDeviceOptions(devOpt, false));
		}
		_log.Log(LOG_STATUS,  "add new device successful model:%s, ssid:%08X", model.c_str(), ssid);
	}

	return true;
}



void XiaomiGateway::updateTemperature(const std::string &nodeID, const std::string &name, const float temperature, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		SendTempSensor(sID, battery, temperature, name);
	}
}

void XiaomiGateway::updateHumidity(const std::string &nodeID, const std::string &name, const int humidity, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		SendHumiditySensor(sID, battery, humidity, name);
	}
}

void XiaomiGateway::updatePressure(const std::string &nodeID, const std::string &name, const float pressure, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		SendPressureSensor((sID & 0xff00)>>8, sID & 0Xff, battery, pressure, name);
	}
}

void XiaomiGateway::updateTempHumPressure(const std::string &nodeID, const std::string &name, const std::string& temperature, const std::string& humidity, const std::string& pressure, const int battery)
{
	bool res = true;
	float lastTemp= 0.0;
	int   lastHum = 0;
	int   lastHumStatus = 0;
	int   lastPre = 0;
	int   lastForecast = 0;
	float temp = 0.0;
	int   hum  = 0;
	float pre  = 0.0;
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID == 0){
		return;
	}

	temp = (float)atof(temperature.c_str()) / 100.0f;
	hum  =  atoi(humidity.c_str()) / 100;
	pre  = static_cast<float>(atof(pressure.c_str())) / 100.0f;

	if (temperature == "" || humidity == "" || pressure == ""){
		auto ssid = WeatherOutlet::idConvert(nodeID);
		std::string sValue = "";
		int type    = pTypeTEMP_HUM_BARO;
		int subType = sTypeTHB1;
		int nValue  = 0;
		struct tm updateTime;

		std::cout<<"ssid.first"<<ssid.first<<"ssid.second"<<ssid.second<<std::endl;

		res = m_sql.GetLastValue(m_HwdID, ssid.second.c_str(), 1, type, subType, nValue, sValue, updateTime);
		if(false == res){
			_log.Log(LOG_ERROR, "get TempHumPressure sValue failed");
			return;
		}

		int n = sscanf(sValue.c_str(), "%f;%d;%d;%d;%d",  &lastTemp, &lastHum, &lastHumStatus, &lastPre, &lastForecast);
		if (n == 5){
			if (temperature == ""){
				temp = lastTemp;
			}
			if (humidity == ""){
				hum = lastHum;
			}
			if (pressure == ""){
				pre = lastPre;
			}
		}
		else{
			_log.Log(LOG_STATUS, "get temphumpress sscanf failed string:%s n:%d", sValue.c_str(), n);
		}
	}

	int Forecast = baroForecastNoInfo;
	if (pre < 1000)
		Forecast = baroForecastRain;
	else if (pre < 1020)
		Forecast = baroForecastCloudy;
	else if (pre < 1030)
		Forecast = baroForecastPartlyCloudy;
	else
		Forecast = baroForecastSunny;

	SendTempHumBaroSensor(sID, battery, temp, hum, pre, Forecast, name);

}

void XiaomiGateway::updateTempHum(const std::string &nodeID, const std::string &name, const std::string& temperature, const std::string&  humidity, const int battery)
{
	float lastTemp= 0.0;
	int   lastHum = 0;
	int   lastHumStatus = 0;
	float temp = 0.0;
	int   hum  = 0;
	bool  res = true;

	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID == 0){
		return;
	}
	temp = (float)atof(temperature.c_str()) / 100.0f;
	hum =  atoi(humidity.c_str()) / 100;

	if (temperature == "" || humidity == ""){

		auto ssid = WeatherOutlet::idConvert(nodeID);
		std::string sValue = "";
		int type    = pTypeTEMP_HUM;
		int subType = sTypeTH5;
		int nValue  = 0;
		struct tm updateTime;

		res = m_sql.GetLastValue(m_HwdID, ssid.second.c_str(), 1, type, subType, nValue, sValue, updateTime);
		if(false == res){
			_log.Log(LOG_ERROR, "get TempHumPressure sValue failed");
			return;
		}

		int n = sscanf(sValue.c_str(), "%f;%d;%d",  &lastTemp, &lastHum, &lastHumStatus);
		if (n == 3){

			if (temperature == ""){
				temp = lastTemp;
			}
			if (humidity == ""){
				hum = lastHum;
			}
		}
		else{
			_log.Log(LOG_STATUS, "get updateTempHum sscanf failed string:%s n:%d", sValue.c_str(), n);
		}

	}

	SendTempHumSensor(sID, battery, temp, hum, name);
}


void XiaomiGateway::updateRGBLight(const std::string & nodeId, const unsigned char unit, const unsigned char subType,const int onOff, const std::string& brightness, const _tColor&  color, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeId);
	if (sID == 0){
		return;
	}
	int lastLevel = 0;
	int nValue = 0;
	int bright = 0;

	bright = atoi(brightness.c_str());

	std::cout<<"updateRGBLight bright string:"<<brightness<<"  bright:"<<bright<<std::endl;
	std::cout<<"color json:"<<color.toJSONString()<<std::endl;

	std::vector<std::vector<std::string> > result;

	auto ssid = LedOutlet::idConvert(nodeId);
	result = m_sql.safe_query("SELECT nValue, LastLevel FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d)", m_HwdID, ssid.second.c_str(), pTypeColorSwitch);
	if (result.empty())
	{
		_log.Log(LOG_ERROR, "not find the device, mac:%s", nodeId.c_str());
		return;
	}

	nValue = atoi(result[0][0].c_str());
	lastLevel = atoi(result[0][1].c_str());
	bright = (brightness == "")? lastLevel : bright;

	if (nValue == gswitch_sOff)
	{
		_log.Log(LOG_ERROR, "led off , do not update all value");
		return;
	}
	SendRGBWSwitch(sID, unit, subType, onOff, bright, color, battery);

	std::cout<<"nValue:"<<nValue<<"	 lastlevel:"<<lastLevel<<"  bright:"<<bright<<std::endl;
}


void XiaomiGateway::updateRGBGateway(const std::string & nodeID, const std::string & name, const bool bIsOn, const int brightness, const int hue)
{
	if (nodeID.length() < 12) {
		_log.Log(LOG_ERROR, "XiaomiGateway: Node ID %s is too short", nodeID.c_str());
		return;
	}
	std::string str = nodeID.substr(4, 8);
	unsigned int sID;
	std::stringstream ss;
	ss << std::hex << str.c_str();
	ss >> sID;

	char szDeviceID[300];
	if (sID == 1)
		sprintf(szDeviceID, "%d", 1);
	else
		sprintf(szDeviceID, "%08X", (unsigned int)sID);

	int lastLevel = 0;
	int nValue = 0;
	bool tIsOn = !(bIsOn);
	std::vector<std::vector<std::string> > result;
	result = m_sql.safe_query("SELECT nValue, LastLevel FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d) AND (SubType==%d)", m_HwdID, szDeviceID, pTypeColorSwitch, sTypeColor_RGB_W);

	if (result.empty()){

		_log.Log(LOG_STATUS, "XiaomiGateway: New Gateway Found (%s/%s)", str.c_str(), name.c_str());
		int cmd = Color_LedOn;
		if (!bIsOn) {
			cmd = Color_LedOff;
		}
		_tColorSwitch ycmd;
		ycmd.subtype = sTypeColor_RGB_W;
		ycmd.id = sID;
		//ycmd.dunit = 0;
		ycmd.value = brightness;
		ycmd.command = cmd;
		m_mainworker.PushAndWaitRxMessage(this, (const unsigned char *)&ycmd, NULL, -1);
		m_sql.safe_query("UPDATE DeviceStatus SET name='%q', SwitchType=%d, LastLevel=%d WHERE(HardwareID == %d) AND (DeviceID == '%s') AND (Type == %d)", name.c_str(), static_cast<int>(STYPE_Dimmer), brightness, m_HwdID, szDeviceID, pTypeColorSwitch);

	}
	else {
		nValue = atoi(result[0][0].c_str());
		tIsOn = (nValue != 0);
		lastLevel = atoi(result[0][1].c_str());
		if ((bIsOn != tIsOn) || (brightness != lastLevel)){
			int cmd = Color_LedOn;
			if (!bIsOn) {
				cmd = Color_LedOff;
			}
			_tColorSwitch ycmd;
			ycmd.subtype = sTypeColor_RGB_W;
			ycmd.id = sID;
			ycmd.value = brightness;
			ycmd.command = cmd;
			m_mainworker.PushAndWaitRxMessage(this, (const unsigned char *)&ycmd, NULL, -1);
		}
	}
}

void XiaomiGateway::updateSwitch(const std::string &nodeID, const std::string &name, const bool bIsOn, const _eSwitchType switchType, const int unit, const int level, const std::string &messageType,  const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID == 0){
		return;
	}
	_tGeneralSwitch xcmd;
	xcmd.len = sizeof(_tGeneralSwitch) - 1;
	xcmd.id = sID;
	xcmd.type = pTypeGeneralSwitch;
	xcmd.subtype = sSwitchGeneralSwitch;
	xcmd.unitcode = unit;
	xcmd.level = level;

	if (bIsOn){
		xcmd.cmnd = gswitch_sOn;
	}
	else{
		xcmd.cmnd = gswitch_sOff;
	}

	if (switchType == STYPE_Selector){

		xcmd.subtype = sSwitchTypeSelector;
		if (level > 0){
			xcmd.cmnd = gswitch_sSetLevel;
			xcmd.level = level;
		}
	}
	else if (switchType == STYPE_SMOKEDETECTOR){
		xcmd.level = level;
	}
	else if (switchType == STYPE_BlindsPercentage){
		xcmd.level = level;
		xcmd.subtype = sSwitchBlindsT2;
		xcmd.cmnd = gswitch_sSetLevel;
	}

	char szTmp[64];
	sprintf(szTmp, "%08X", (unsigned int)sID);
	std::string ID = szTmp;

	auto result = m_sql.safe_query("SELECT nValue FROM DeviceStatus WHERE (HardwareID!=%d) AND (DeviceID=='%q') AND (Type==%d) AND (Unit == '%d')", m_HwdID, ID.c_str(), xcmd.type, xcmd.unitcode);
	if (!result.empty()){
		_log.Log(LOG_STATUS, "XiaomiGateway: the device is added for another gateway hardware id");
		return;
	}

	result = m_sql.safe_query("SELECT nValue, sValue, batteryLevel FROM DeviceStatus WHERE (HardwareID==%d) AND (DeviceID=='%q') AND (Type==%d) AND (Unit == '%d')", m_HwdID, ID.c_str(), xcmd.type, xcmd.unitcode);
	if (result.empty())
	{
		_log.Log(LOG_ERROR, "XiaomiGateway: the device not in database, mac:%s", nodeID.c_str());
		return ;
	}

	int nValue = atoi(result[0][0].c_str());
	int sValue = atoi(result[0][1].c_str());
	int batteryLevel = atoi(result[0][2].c_str());

	if (messageType == "heartbeat") {
		if (battery != 255) {
			batteryLevel = battery;
			m_sql.safe_query("UPDATE DeviceStatus SET batteryLevel=%d WHERE(HardwareID == %d) AND (DeviceID == '%q') AND (Unit == '%d')", batteryLevel, m_HwdID, ID.c_str(), xcmd.unitcode);
		}
	}
	else {
		if ((bIsOn == false && nValue >= 1) ||
			(bIsOn == true && nValue == 0 && switchType == STYPE_OnOff) ||
			(bIsOn == true && switchType != STYPE_OnOff)) {

			m_mainworker.PushAndWaitRxMessage(this, (const unsigned char *)&xcmd, NULL, batteryLevel);
		}
	}
}

void XiaomiGateway::updateCubeText(const std::string & nodeID, const std::string & name, const std::string &degrees)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		SendTextSensor(sID>>8, sID & 0xff, 255, degrees.c_str(), name);
	}
}

void XiaomiGateway::updateVoltage(const std::string & nodeID, const std::string & name, const int voltage)
{
	if (voltage < 3600) {
		unsigned int sID = OutletAttr::macToUint(nodeID);
		if (sID > 0) {
			int percent = ((voltage - 2200) / 10);
			float vol = (float)voltage / 1000;
			SendVoltageSensor(sID>>8, sID & 0xff, percent, vol, "Xiaomi Voltage");
		}
	}
}

void XiaomiGateway::updateLux(const std::string & nodeID, const unsigned char unit, const std::string & name, const int illumination, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		float lux = (float)illumination;
		SendLuxSensor(sID, unit, battery, lux, name);
	}
}

void XiaomiGateway::updateKwh(const std::string & nodeID, const std::string & name, const std::string& loadPower, const std::string& consumed, const int battery)
{
	unsigned int sID = OutletAttr::macToUint(nodeID);
	if (sID > 0) {
		if (loadPower != "" || consumed != "")
		{
			double power = atof(loadPower.c_str());
			double consume = atof(consumed.c_str()) / 1000;
			SendKwhMeter(sID>>8 , sID & 0xff, 255, power, consume, name);
		}
	}
}


UdpServer::UdpServer(const std::shared_ptr<boost::asio::io_service> &ioService, const std::string &localIp)
	: socket_(*ioService, boost::asio::ip::udp::v4())
{
	m_localIp = localIp;
	setIoService(ioService);
	std::cout<<"UdpServer and localIp"<<localIp<<std::endl;
}

UdpServer::~UdpServer()
{
	if (UdpServer::getIoService()){
		_log.Log(LOG_STATUS, "io service is still work");
	}

	if (UdpServer::getUdpServer()){
		_log.Log(LOG_STATUS, "upd server is still work");
	}
	_log.Log(LOG_STATUS, "~~UdpServer");
}

bool UdpServer::initServer()
{

	try {
		socket_.set_option(boost::asio::ip::udp::socket::reuse_address(true));
		_log.Log(LOG_ERROR, "XiaomiGateway: UdpServer m_localip： %s", UdpServer::m_localIp.c_str());

		if (UdpServer::m_localIp != "") {
			boost::system::error_code ec;
			boost::asio::ip::address listenAddr = boost::asio::ip::address::from_string(UdpServer::m_localIp, ec);
			boost::asio::ip::address mcastAddr = boost::asio::ip::address::from_string("224.0.0.50", ec);
			boost::asio::ip::udp::endpoint listenEndpoint(mcastAddr, 9898);

			socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 9898));
			socket_.set_option(boost::asio::ip::multicast::join_group(mcastAddr.to_v4(), listenAddr.to_v4()), ec);
			socket_.bind(listenEndpoint, ec);
		}
		else {
			socket_.bind(boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 9898));
			socket_.set_option(boost::asio::ip::multicast::join_group(boost::asio::ip::address::from_string("224.0.0.50")));
		}
	}
	catch (const boost::system::system_error& ex) {

		_log.Log(LOG_ERROR, "XiaomiGateway: %s", ex.code().category().name());
		resetIoService();
		return false;
	}
	startReceive();

	return true;
}


void UdpServer::startReceive()
{
	memset(&data_[0], 0, sizeof(data_));
	socket_.async_receive_from(boost::asio::buffer(data_, max_length), remote_endpoint_, boost::bind(&UdpServer::handleReceive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}


bool UdpServer::run()
{
	auto io = UdpServer::getIoService();
	if (io)
	{
		m_serThread = boost::thread(boost::bind(&boost::asio::io_service::run, io.get()));
		SetThreadName(m_serThread.native_handle(), "XiaomiGwIO");
		return true;
	}
	return false;
}

void UdpServer::stop()
{
	std::cout<<"UdpServer stop"<<std::endl;
	auto io = UdpServer::getIoService();
	io->stop();
	m_serThread.join();
	UdpServer::resetIoService();
	UdpServer::resetUdpServer();
}



bool UdpServer::recvParamCheck(Json::Value& root)
{
	if (false == root.isMember(keyCmd) ||
		false == root[keyCmd].isString()
		)
	{
		_log.Log(LOG_ERROR, "ParamCheck: Receive the message cmd  not exist or not string");
		return false;
	}

	std::string cmd = root[keyCmd].asString();

	if (cmd.empty() )
	{
		_log.Log(LOG_ERROR, "ParamCheck: Receive the message cmd empty");
		return false;
	}

	/* cmd model */
	if ((cmd == rspRead) ||
		(cmd == rspWrite) ||
		(cmd == repReport) ||
		(cmd == repHBeat))
	{
		if (false == root.isMember(keySid) ||
			false == root.isMember(keyModel)||
			false == root.isMember(keyParams)||
			false == root[keySid].isString() ||
			false == root[keyModel].isString() ||
			false == root[keyParams].isArray())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message cmd or model params error");
			return false;
		}

		std::string sid = root[keySid].asString();
		std::string model = root[keyModel].asString();
		if (sid.empty() || model.empty())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message sid or model empty");
			return false;
		}

		if (!XiaomiGateway::checkZigbeeMac(sid))
		{
			_log.Log(LOG_ERROR, "ParamCheck: Device sid(mac:%s) is invalid", sid.c_str());
			return false;
		}

		Json::Value params =  root[keyParams];
		if (params.size() <= 0)
		{
			_log.Log(LOG_ERROR, "ParamCheck: Params is null at json");
			return false;
		}
		return true;
	}


	if (cmd == rspDiscorey)
	{
		if (false == root.isMember(keySid) ||
			false == root.isMember(keyToken)||
			false == root.isMember(keyDevList)||
			false == root[keySid].isString() ||
			false == root[keyToken].isString())
/*
		||
			false == root[keyDevList].isArray())
*/
		{
			_log.Log(LOG_ERROR, "Receive the message miss sid token dev_list error");
			return false;
		}

		std::string sid = root[keySid].asString();
		std::string token = root[keyToken].asString();
		if (sid.empty() || token.empty())
		{
			_log.Log(LOG_ERROR, "ParamCheck: sid or token is empty at json");
			return false;
		}

		if (!XiaomiGateway::checkZigbeeMac(sid))
		{
			_log.Log(LOG_ERROR, "ParamCheck: Device sid(mac) is invalid");
			return false;
		}

		return true;
	}


	if (cmd == rspWhoIs)
	{
		if (false == root.isMember(keyIp) ||
			false == root.isMember(keyProtocol) ||
			false == root.isMember(keyPort) ||
			false == root.isMember(keyModel)||
			false == root[keyIp].isString() ||
			false == root[keyProtocol].isString() ||
			false == root[keyPort].isString() ||
			false == root[keyModel].isString())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message miss ip protocal port model");
			return false;
		}

		std::string ip = root[keyIp].asString();
		std::string protocol = root[keyProtocol].asString();
		std::string port = root[keyPort].asString();
		std::string model = root[keyModel].asString();
		if (ip.empty() || protocol.empty() || port.empty() || model.empty())
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message ip protocal port model value null");
			return false;
		}

		struct sockaddr_in addr;
		int ret = ::inet_pton(AF_INET, ip.c_str(), (void*)&addr.sin_addr.s_addr);
		if (ret != 1)
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message ip convert error");
			return false;
		}

		try
		{
			ret = std::stoi(port);
			if (ret < 1024 || ret > 65535)
			{
				throw std::range_error("port outof range");
			}
		}

		catch(std::exception & e)
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message port(%s) error: %s", port.c_str(),  e.what());
			return false;
		}

		if (protocol != "UDP")
		{
			_log.Log(LOG_ERROR, "ParamCheck: Receive the message protocol value error, expect UDP");
			return false;
		}
		return true;
	}

	_log.Log(LOG_ERROR, "ParamCheck: Cmd not support, cmd:%s", cmd.c_str());
	return false;
}



void UdpServer::handleReceive(const boost::system::error_code & error, std::size_t bytes_recvd)
{

	if (error && error != boost::asio::error::message_size){
		_log.Log(LOG_ERROR, "XiaomiGateway: error in handleReceive %s", error.message().c_str());
		return;
	}

	std::string remoteAddr = remote_endpoint_.address().to_v4().to_string();
	XiaomiGateway * gateway = XiaomiGateway::getGatewayByIp(remoteAddr);
	if (!gateway){
		_log.Log(LOG_ERROR, "XiaomiGateway: received data from  unregisted gateway ip:%s!", remoteAddr.c_str());
		startReceive();
		return;
	}

#define _DEBUG
#ifdef _DEBUG
	_log.Log(LOG_STATUS, "%s", data_);
#endif

	Json::Value root;
	bool showmessage = true;
	std::string message(data_);
	bool ret;

	ret = ParseJSon(message, root);
	if (false == ret || false == root.isObject()){
		_log.Log(LOG_ERROR, "XiaomiGateway: the received data json format error!");
		startReceive();
		return;
	}

	if (!recvParamCheck(root)){
		_log.Log(LOG_ERROR, "XiaomiGateway: param check error!");
		startReceive();
		return;
	}

	Json::Value params;
	std::string cmd;
	std::string model;
	std::string sid;

	cmd = root[keyCmd].asString();
	model = root[keyModel].asString();

	if ((cmd == rspRead) || (cmd == rspWrite) ||
		(cmd == repReport) || (cmd == repHBeat)){

		sid = root[keySid].asString();
		std::shared_ptr <Device> dev = gateway->getDevice(sid);
		if (!dev){
			_log.Log(LOG_ERROR, "XiaomiGateway: not find the device at list! sid:%s", sid.c_str());
			startReceive();
			return;
		}

		if(dev->getZigbeeModel() != model){
			_log.Log(LOG_ERROR, "XiaomiGateway: model not match, expect:%s, but:%s sid:%s",
					dev->getZigbeeModel().c_str(), model.c_str(), sid.c_str());
			startReceive();
			return;
		}

		ReadParam param;
		param.message  = message;
		param.miGateway = static_cast<void*>(gateway);
		dev->recvFrom(param);
		startReceive();
		return;
	}


	if (cmd == rspDiscorey){
		gateway->deviceListHandler(root);
		startReceive();
		return;
	}


	if (cmd == rspWhoIs){
		gateway->joinGatewayHandler(root);
		startReceive();
		return;
	}

	_log.Log(LOG_STATUS, " recv Param Check have some problem, call soft developer cmd:%s", cmd.c_str());
	startReceive();
}


TokenManager& TokenManager::getInstance()
{
	static TokenManager instance;
	return instance;
}

void TokenManager::updateTokenSid(const std::string & ip, const std::string & token, const std::string & sid)
{
	bool found = false;
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_gwTokens.size(); i++) {
		if (boost::get<0>(m_gwTokens[i]) == ip) {
			boost::get<1>(m_gwTokens[i]) = token;
			boost::get<2>(m_gwTokens[i]) = sid;
			found = true;
		}
	}
	if (!found) {
		m_gwTokens.push_back(boost::make_tuple(ip, token, sid));
	}

}

std::string TokenManager::getToken(const std::string & ip)
{
	std::string token = "";
	bool found = false;
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_gwTokens.size(); i++) {
		if (boost::get<0>(m_gwTokens[i]) == ip) {
			token = boost::get<1>(m_gwTokens[i]);
		}
	}
	return token;
}

std::string TokenManager::getSid(const std::string & ip)
{
	std::string sid = "";
	bool found = false;
	std::unique_lock<std::mutex> lock(m_mutex);
	for (unsigned i = 0; i < m_gwTokens.size(); i++) {
		if (boost::get<0>(m_gwTokens[i]) == ip) {
			sid = boost::get<2>(m_gwTokens[i]);
		}
	}
	return sid;
}


namespace http {
	namespace server {

		void CWebServer::Cmd_AddZigbeeDevice(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != 2){
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			std::string hwid = request::findValue(&req, "idx");
			std::string cmd = request::findValue(&req, "command");
			std::string model = request::findValue(&req, "model");

			if (hwid == "" || (cmd != "On" && cmd != "Off")){
				_log.Log(LOG_ERROR, "AddZigbeeDevice command is outof range");
				return;
			}

			int iHardwareID = atoi(hwid.c_str());
			CDomoticzHardwareBase *pHardware = m_mainworker.GetHardware(iHardwareID);
			if (pHardware == NULL){
				_log.Log(LOG_ERROR, "AddZigbeeDevice hardware is not find");
				return;
			}

			if (pHardware->HwdType != HTYPE_XiaomiGateway){
				_log.Log(LOG_ERROR, "AddZigbeeDevice hardware type is not XiaomiGateway");
				return;
			}
			bool res = m_mainworker.AddZigbeeDevice(hwid, cmd, model);
			if (false == res){
				root["status"] = "ERR";
			}
			else{
				root["status"] = "OK";
				root["title"] = "AddZigbeeDevice";
			}
		}

		void CWebServer::Cmd_GetNewDevicesList(WebEmSession & session, const request& req, Json::Value &root)
		{
			static int sHwId = 0;
			static int sRandom = 0;
			static std::map< std::string, std::vector<std::string> > sOldList;

			if (session.rights != 2){
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string hwid = request::findValue(&req, "idx");
			std::string random = request::findValue(&req, "random");
			if (hwid == "" || random == ""){
				_log.Log(LOG_ERROR, "Cmd_GetNewDevicesList idx or random empty");
				return;
			}

			int tmp;
			int iHardwareID;

			try{
				tmp = std::stoi(random);
				iHardwareID = std::stoi(hwid);
			}

			catch(std::exception& e){
				_log.Log(LOG_ERROR, "get random(%s) or hardware(%s) id failed:%s", random.c_str(), hwid.c_str(), e.what());
			}

			std::cout<<"business random:"<<sRandom<< "  now:"<<tmp<<std::endl;
			root["status"] = "OK";
			root["title"] = "GetNewDevicesList";

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT Mac, name, Model FROM DeviceStatus WHERE (HardwareID=%d)", iHardwareID);

			/* start of get new list */
			if(tmp != sRandom){

				sRandom	= tmp;
				sOldList.clear();
				for (const auto &itt : result){
					sOldList.insert(std::make_pair(itt[0], itt));
				}
				/* if dev_list null, add null at [0] */
				root["dev_list"][0];
				return;
			}

			int ii = 0;

			for (const auto &itt : result){

				auto iter = sOldList.find(itt[0]);
				/* no find at old list */
				if (iter == sOldList.end()){
					root["dev_list"][ii]["name"] = itt[1];
					root["dev_list"][ii]["Model"] = itt[2];
					root["dev_list"][ii]["Mac"] = itt[0];
					ii++;
				}
			}
			/* if dev_list null, add null at [0] */
			if (0 == ii){
				root["dev_list"][0];
			}

		}

		}
}
