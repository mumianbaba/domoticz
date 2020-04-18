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


inline unsigned int OutletAttr::macToUint(const std::string& mac)
{
	unsigned long long sID = std::stoull(mac, 0, 16);
	return (sID & 0xffffffff);
}


SsidPair OutletAttr::idConverter(const std::string& mac) const
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






