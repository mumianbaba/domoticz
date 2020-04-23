#include <iostream>
#include <map>
#include "Outlet.hpp"
#include "DevAttr.hpp"
#include "Device.hpp"

namespace XiaoMi{

/* OutletAttr */
std::ostream & operator << (std::ostream& out, OutletAttr& outletAttr)
{
	out<<"type:"<<std::hex<<outletAttr.m_type<<std::endl;
	out<<"subType:"<<std::hex<<outletAttr.m_subType<<std::endl;
	out<<"swType:"<<outletAttr.m_swType<<std::endl;
	out<<"unit:"<<outletAttr.m_unit<<std::endl;
	out<<"direction:"<<outletAttr.m_direction<<std::endl;
	out<<"opts:"<<outletAttr.m_opts<<std::endl;
	return out;
}


/* OutletAttr */
std::ostream & operator << (std::ostream& out, OutletAttr* outletAttr)
{
	if (outletAttr == nullptr)
	{
		out<<"ptr null"<<std::endl;
		return out;
	}
	out<<"type:"<<std::hex<<outletAttr->m_type<<std::endl;
	out<<"subType:"<<std::hex<<outletAttr->m_subType<<std::endl;
	out<<"swType:"<<outletAttr->m_swType<<std::endl;
	out<<"unit:"<<outletAttr->m_unit<<std::endl;
	out<<"direction:"<<outletAttr->m_direction<<std::endl;
	out<<"opts:"<<outletAttr->m_opts<<std::endl;
	return out;
}


/* DevAttr */
std::ostream & operator << (std::ostream& out, DevAttr& devAttr)
{
	out<<std::endl;
	out<<"---------devattr----------------"<<std::endl;
	out<<"name:"<<devAttr.m_name<<std::endl;
	out<<"zigbeeModel:"<<devAttr.m_zigbeeModel<<std::endl;
	out<<"model:"<<devAttr.m_model<<std::endl;
	out<<"vendor:"<<devAttr.m_vendor<<std::endl;
	out<<"outlet number:"<<devAttr.m_Outlet.size()<<std::endl;
	out<<std::endl;
	int ii = 0;
	for (const auto &itt : devAttr.m_Outlet)
	{
		out<<"---------outlet:"<<ii++<<"--------"<<std::endl;
		out<<itt;
		out<<std::endl;
	}
	out<<std::endl;
	return out;
}


std::ostream & operator << (std::ostream& out, DevAttr const & devAttr)
{
	out<<std::endl;
	out<<"---------devattr----------------"<<std::endl;
	out<<"name:"<<devAttr.m_name<<std::endl;
	out<<"zigbeeModel:"<<devAttr.m_zigbeeModel<<std::endl;
	out<<"model:"<<devAttr.m_model<<std::endl;
	out<<"vendor:"<<devAttr.m_vendor<<std::endl;
	out<<"outlet number:"<<devAttr.m_Outlet.size()<<std::endl;
	out<<std::endl;
	int ii = 0;
	for (const auto &itt : devAttr.m_Outlet)
	{
		out<<"---------outlet:"<<ii++<<"--------"<<std::endl;
		out<<itt;
		out<<std::endl;
	}
	out<<std::endl;
	return out;
}

/* DevID */
std::ostream & operator << (std::ostream& out, DevID& devID)
{
	out<<"mac:"<<devID.m_mac<<std::endl;
	for (const auto &itt : devID.m_ssidList)
	{
		out<<"ssid int:"<<std::hex<<itt.first<<std::endl;
		out<<"ssid string:"<<itt.second<<std::endl;
	}
	return out;
}

/* Device */
std::ostream & operator << (std::ostream& out, Device& device)
{
	out<<device.m_devID<<std::endl;
	out<<*(device.m_devAttr)<<std::endl;
}

}

