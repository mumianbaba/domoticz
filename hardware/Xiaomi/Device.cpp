#include <iostream>
#include <string>
#include <map>
#include "Device.hpp"
#include "DevAttr.hpp"
#include "Outlet.hpp"
#include "../../main/Logger.h"

//using namespace XiaoMi;



namespace XiaoMi{


bool DevID::match(std::string& mac) const
{
	return (m_mac == mac);
}

bool DevID::match(unsigned int ssid) const
{
	for (const auto &itt : m_ssidList)
	{
		if (itt.first == ssid)
		{
			return true;
		}
	}
	return false;
}





/*
 *  class  Device
 *
 *
*/
Device::Device(std::string mac, const DevAttr* devAttr):m_devAttr(devAttr)
{
	std::vector<SsidPair> result;
	for(int ii = 0; ;ii++)
	{
		auto outlet = m_devAttr->getOutlet(ii);
		if (outlet == nullptr)
		{
			break;
		}
		SsidPair ssid = outlet->idConverter(mac);
		result.emplace_back(ssid);
	}

	m_devID.initID(mac, result);
	m_online = OnlineStatus::Unknown;
	m_timestamp = time(nullptr);
}



bool  Device::writeTo(WriteParam& param)
{
	int type = param.type;
	int subtype = param.subType;
	int unit = param.unit;

	if (OnlineStatus::Offline == m_online)
	{
		_log.Log(LOG_STATUS, "writeTo:the device is offline, can't control. model:%s Mac:%s",
							  getZigbeeModel().c_str(), getMac().c_str());
		return false;
	}
	param.mac = getMac();
	param.model = getZigbeeModel();

	bool res = false;
	auto Outlet = getOutlet();
	for (const auto & itt : Outlet)
	{
		if (itt->match(type, subtype, unit) == true)
		{
			res = itt->writeTo(param);
			break;
		}
	}
	return res;
}

void Device::recvFrom(ReadParam& param)
{
	bool res = false;
	auto Outlet = m_devAttr->getOutlet();
	for (const auto & itt : Outlet)
	{
		itt->recvFrom(param);
	}
	m_timestamp = time(nullptr);
	_log.Log(LOG_STATUS, "recvFrom timestamp %ld . model:%s mac:%s",
							m_timestamp, getZigbeeModel().c_str(), getMac().c_str());

	return;
}


bool Device::match(std::string& mac) const
{
	return m_devID.match(mac);
}


bool Device::match(unsigned int ssid, int type, int subType, int unit) const
{
	return (m_devID.match(ssid) && m_devAttr->match(type, subType, unit));
}


void Device::updateTimestamp(time_t t)
{
	m_timestamp = t;
	_log.Log(LOG_STATUS, "updateTimestamp timestamp %ld . model:%s mac:%s",
							m_timestamp, getZigbeeModel().c_str(), getMac().c_str());
}

time_t Device::getTimestamp()
{
	return m_timestamp;
}
OnlineStatus Device::getOnline()
{
	return m_online;
}
void  Device::setOnline(bool status)
{
	if (status)
	{
		m_online = OnlineStatus::Online;
	}
	else
	{
		m_online = OnlineStatus::Offline;
	}
}

}


