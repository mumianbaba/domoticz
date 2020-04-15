#include "Outlet.hpp"
#include <iostream>
#include <string>

//#include "../hardwaretypes.h"
//#include "../../main/RFXNames.h"

using namespace XiaoMi;

OutletAttr::OutletAttr(int type, int subType, int swType, int unit, int direction, std::string opts)
:m_type(type), m_subType(subType),m_swType(swType),m_unit(unit),m_direction(direction),m_opts(opts)
{

}


inline unsigned int OutletAttr::macToUint(std::string& mac) const
{
	unsigned long long sID = std::stoull(mac, 0, 16);
	return (sID & 0xffffffff);
}


SsidPair OutletAttr::idConverter(std::string& mac) const
{
	
	unsigned int rowId;
	char szTmp[64];

	rowId = macToUint(mac);
	sprintf(szTmp, "%08X", rowId);
	std::string  strSsid = szTmp;
	return std::make_pair(rowId, strSsid);
}

bool OutletAttr::match(int type, int subType, int unit) const
{
	if (type == m_type && subType == m_subType && unit == m_unit)
	{
		return true;
	}
	return false;
}


#if 0

OnOffOutlet::OnOffOutlet(int unit, int dir, std::initializer_list<Rule_OnOff> list)
	:OutletAttr(pTypeGeneralSwitch, sSwitchGeneralSwitch, static_cast<int>(::STYPE_OnOff), static_cast<int>(unit), static_cast<int>(dir), "")
{
	std::cout<<"OnOffOutlet init list size:"<<list.size()<<std::endl;
	for (const auto &itt : list)
	{
		m_rule.emplace_back(itt);
	}
	std::cout<<"OnOffOutlet rule number:"<<list.size()<<std::endl;
}


bool OnOffOutlet::recvFrom(std::string& root, void * miGateway) const
{
	std::cout<<"OnOffOutlet recvFrom"<<std::endl;
}

bool OnOffOutlet::writeTo(const unsigned char* packet, int len, std::string& mac, std::string& model,std::string& gwMac,  std::string& key, void * miGateway) const
{
	std::cout<<"OnOffOutlet writeTo"<<std::endl;
}


#endif





