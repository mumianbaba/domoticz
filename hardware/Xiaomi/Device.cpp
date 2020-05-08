#include <iostream>
#include <string>
#include <map>
#include "Device.hpp"
#include "DevAttr.hpp"
#include "Outlet.hpp"

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
}



bool  Device::writeTo(WriteParam& param)
{
	int type = param.type;
	int subtype = param.subType;
	int unit = param.unit;

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



}


