#include "stdafx.h"
#include <iostream>
#include "Scheduler.h"
#include "localtime_r.h"
#include "Logger.h"
#include "Helper.h"
#include "SQLHelper.h"
#include "mainworker.h"
#include "WebServer.h"
#include "HTMLSanitizer.h"
#include "../webserver/cWebem.h"
#include "../json/json.h"
#include "boost/date_time/gregorian/gregorian.hpp"
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

CScheduler::CScheduler(void)
{
	m_tSunRise = 0;
	m_tSunSet = 0;
	m_tSunAtSouth = 0;
	m_tCivTwStart = 0;
	m_tCivTwEnd = 0;
	m_tNautTwStart = 0;
	m_tNautTwEnd = 0;
	m_tAstTwStart = 0;
	m_tAstTwEnd = 0;
	srand((int)mytime(NULL));
}

CScheduler::~CScheduler(void)
{
}

void CScheduler::StartScheduler()
{
	m_thread = std::make_shared<std::thread>(&CScheduler::Do_Work, this);
	SetThreadName(m_thread->native_handle(), "Scheduler");
}

void CScheduler::StopScheduler()
{
	if (m_thread)
	{
		RequestStop();
		m_thread->join();
		m_thread.reset();
	}
}

std::vector<tScheduleItem> CScheduler::GetScheduleItems()
{
	std::lock_guard<std::mutex> l(m_mutex);
	std::vector<tScheduleItem> ret;
	for (const auto & itt : m_scheduleitems)
		ret.push_back(itt);
	return ret;
}

void CScheduler::ReloadSchedules()
{
	std::lock_guard<std::mutex> l(m_mutex);
	m_scheduleitems.clear();

	std::vector<std::vector<std::string> > result;

	time_t atime = mytime(NULL);
	struct tm ltime;
	localtime_r(&atime, &ltime);

	//Add Device Timers
	result = m_sql.safe_query(
		"SELECT T1.DeviceRowID, T1.Time, T1.Type, T1.Cmd, T1.Level, T1.Days, T2.Name,"
		" T2.Used, T1.UseRandomness, T1.Color, T1.[Date], T1.MDay, T1.Month, T1.Occurence, T1.ID"
		" FROM Timers as T1, DeviceStatus as T2"
		" WHERE ((T1.Active == 1) AND (T1.TimerPlan == %d) AND (T2.ID == T1.DeviceRowID))"
		" ORDER BY T1.ID",
		m_sql.m_ActiveTimerPlan);
	if (!result.empty())
	{
		for (const auto & itt : result)
		{
			std::vector<std::string> sd = itt;

			bool bDUsed = (atoi(sd[7].c_str()) != 0);

			if (bDUsed == true)
			{
				tScheduleItem titem;

				titem.bEnabled = true;
				titem.bIsScene = false;
				titem.bIsThermostat = false;

				_eTimerType timerType = (_eTimerType)atoi(sd[2].c_str());
				titem.RowID = std::stoull(sd[0]);
				titem.TimerID= std::stoull(sd[14]);
				titem.startHour = (unsigned char)atoi(sd[1].substr(0, 2).c_str());
				titem.startMin = (unsigned char)atoi(sd[1].substr(3, 2).c_str());
				titem.startTime = 0;
				titem.timerType = timerType;
				titem.timerCmd = (_eTimerCommand)atoi(sd[3].c_str());
				titem.Level = (unsigned char)atoi(sd[4].c_str());
				titem.bUseRandomness = (atoi(sd[8].c_str()) != 0);
				titem.Color = _tColor(sd[9]);
				titem.MDay = 0;
				titem.Month = 0;
				titem.Occurence = 0;

				if (timerType == TTYPE_FIXEDDATETIME)
				{
					std::string sdate = sd[10];
					if (sdate.size() != 10)
						continue; //invalid
					titem.startYear = (unsigned short)atoi(sdate.substr(0, 4).c_str());
					titem.startMonth = (unsigned char)atoi(sdate.substr(5, 2).c_str());
					titem.startDay = (unsigned char)atoi(sdate.substr(8, 2).c_str());
				}
				else if (timerType == TTYPE_MONTHLY)
				{
					std::string smday = sd[11];
					if (smday == "0")
						continue; //invalid
					titem.MDay = atoi(smday.c_str());
				}
				else if (timerType == TTYPE_MONTHLY_WD)
				{
					std::string socc = sd[13];
					if (socc == "0")
						continue; //invalid
					titem.Occurence = atoi(socc.c_str());
				}
				else if (timerType == TTYPE_YEARLY)
				{
					std::string smday = sd[11];
					std::string smonth = sd[12];
					if ((smday == "0") || (smonth == "0"))
						continue; //invalid
					titem.MDay = atoi(smday.c_str());
					titem.Month = atoi(smonth.c_str());
				}
				else if (timerType == TTYPE_YEARLY_WD)
				{
					std::string smonth = sd[12];
					std::string socc = sd[13];
					if ((smonth == "0") || (socc == "0"))
						continue; //invalid
					titem.Month = atoi(smonth.c_str());
					titem.Occurence = atoi(socc.c_str());
				}

				if ((titem.timerCmd == TCMD_ON) && (titem.Level == 0))
				{
//					titem.Level = 100;
				}
				titem.Days = atoi(sd[5].c_str());
				titem.DeviceName = sd[6];
				if (AdjustScheduleItem(&titem, false) == true)
					m_scheduleitems.push_back(titem);
			}
			else
			{
				//not used? delete it
				m_sql.safe_query("DELETE FROM Timers WHERE (DeviceRowID == '%q')",
					sd[0].c_str());
			}
		}
	}

	//Add Scene Timers
	result = m_sql.safe_query(
		"SELECT T1.SceneRowID, T1.Time, T1.Type, T1.Cmd, T1.Level, T1.Days, T2.Name,"
		" T1.UseRandomness, T1.[Date], T1.MDay, T1.Month, T1.Occurence, T1.ID"
		" FROM SceneTimers as T1, Scenes as T2"
		" WHERE ((T1.Active == 1) AND (T1.TimerPlan == %d) AND (T2.ID == T1.SceneRowID))"
		" ORDER BY T1.ID",
		m_sql.m_ActiveTimerPlan);
	if (!result.empty())
	{
		for (const auto & itt : result)
		{
			std::vector<std::string> sd = itt;

			tScheduleItem titem;

			titem.bEnabled = true;
			titem.bIsScene = true;
			titem.bIsThermostat = false;
			titem.MDay = 0;
			titem.Month = 0;
			titem.Occurence = 0;

			{
				std::stringstream s_str(sd[0]);
				s_str >> titem.RowID; }
			{
				std::stringstream s_str(sd[12]);
				s_str >> titem.TimerID; }

			_eTimerType timerType = (_eTimerType)atoi(sd[2].c_str());

			if (timerType == TTYPE_FIXEDDATETIME)
			{
				std::string sdate = sd[8];
				if (sdate.size() != 10)
					continue; //invalid
				titem.startYear = (unsigned short)atoi(sdate.substr(0, 4).c_str());
				titem.startMonth = (unsigned char)atoi(sdate.substr(5, 2).c_str());
				titem.startDay = (unsigned char)atoi(sdate.substr(8, 2).c_str());
			}
			else if (timerType == TTYPE_MONTHLY)
			{
				std::string smday = sd[9];
				if (smday == "0")
					continue; //invalid
				titem.MDay = atoi(smday.c_str());
			}
			else if (timerType == TTYPE_MONTHLY_WD)
			{
				std::string socc = sd[11];
				if (socc == "0")
					continue; //invalid
				titem.Occurence = atoi(socc.c_str());
			}
			else if (timerType == TTYPE_YEARLY)
			{
				std::string smday = sd[9];
				std::string smonth = sd[10];
				if ((smday == "0") || (smonth == "0"))
					continue; //invalid
				titem.MDay = atoi(smday.c_str());
				titem.Month = atoi(smonth.c_str());
			}
			else if (timerType == TTYPE_YEARLY_WD)
			{
				std::string smonth = sd[10];
				std::string socc = sd[11];
				if ((smonth == "0") || (socc == "0"))
					continue; //invalid
				titem.Month = atoi(smonth.c_str());
				titem.Occurence = atoi(socc.c_str());
			}

			titem.startHour = (unsigned char)atoi(sd[1].substr(0, 2).c_str());
			titem.startMin = (unsigned char)atoi(sd[1].substr(3, 2).c_str());
			titem.startTime = 0;
			titem.timerType = timerType;
			titem.timerCmd = (_eTimerCommand)atoi(sd[3].c_str());
			titem.Level = (unsigned char)atoi(sd[4].c_str());
			titem.bUseRandomness = (atoi(sd[7].c_str()) != 0);
			if ((titem.timerCmd == TCMD_ON) && (titem.Level == 0))
			{
//				titem.Level = 100;
			}
			titem.Days = atoi(sd[5].c_str());
			titem.DeviceName = sd[6];
			if (AdjustScheduleItem(&titem, false) == true)
				m_scheduleitems.push_back(titem);
		}
	}

	//Add Setpoint Timers
	result = m_sql.safe_query(
		"SELECT T1.DeviceRowID, T1.Time, T1.Type, T1.Temperature, T1.Days, T2.Name,"
		" T1.[Date], T1.MDay, T1.Month, T1.Occurence, T1.ID"
		" FROM SetpointTimers as T1, DeviceStatus as T2"
		" WHERE ((T1.Active == 1) AND (T1.TimerPlan == %d) AND (T2.ID == T1.DeviceRowID))"
		" ORDER BY T1.ID",
		m_sql.m_ActiveTimerPlan);
	if (!result.empty())
	{
		for (const auto & itt : result)
		{
			std::vector<std::string> sd = itt;

			tScheduleItem titem;

			titem.bEnabled = true;
			titem.bIsScene = false;
			titem.bIsThermostat = true;
			titem.MDay = 0;
			titem.Month = 0;
			titem.Occurence = 0;

			{
				std::stringstream s_str(sd[0]);
				s_str >> titem.RowID; }
			{
				std::stringstream s_str(sd[10]);
				s_str >> titem.TimerID; }

			_eTimerType timerType = (_eTimerType)atoi(sd[2].c_str());

			if (timerType == TTYPE_FIXEDDATETIME)
			{
				std::string sdate = sd[6];
				if (sdate.size() != 10)
					continue; //invalid
				titem.startYear = (unsigned short)atoi(sdate.substr(0, 4).c_str());
				titem.startMonth = (unsigned char)atoi(sdate.substr(5, 2).c_str());
				titem.startDay = (unsigned char)atoi(sdate.substr(8, 2).c_str());
			}
			else if (timerType == TTYPE_MONTHLY)
			{
				std::string smday = sd[7];
				if (smday == "0")
					continue; //invalid
				titem.MDay = atoi(smday.c_str());
			}
			else if (timerType == TTYPE_MONTHLY_WD)
			{
				std::string socc = sd[9];
				if (socc == "0")
					continue; //invalid
				titem.Occurence = atoi(socc.c_str());
			}
			else if (timerType == TTYPE_YEARLY)
			{
				std::string smday = sd[7];
				std::string smonth = sd[8];
				if ((smday == "0") || (smonth == "0"))
					continue; //invalid
				titem.MDay = atoi(smday.c_str());
				titem.Month = atoi(smonth.c_str());
			}
			else if (timerType == TTYPE_YEARLY_WD)
			{
				std::string smonth = sd[8];
				std::string socc = sd[9];
				if ((smonth == "0") || (socc == "0"))
					continue; //invalid
				titem.Month = atoi(smonth.c_str());
				titem.Occurence = atoi(socc.c_str());
			}

			titem.startHour = (unsigned char)atoi(sd[1].substr(0, 2).c_str());
			titem.startMin = (unsigned char)atoi(sd[1].substr(3, 2).c_str());
			titem.startTime = 0;
			titem.timerType = timerType;
			titem.timerCmd = TCMD_ON;
			titem.Temperature = static_cast<float>(atof(sd[3].c_str()));
			titem.Level = 100;
			titem.bUseRandomness = false;
			titem.Days = atoi(sd[4].c_str());
			titem.DeviceName = sd[5];
			if (AdjustScheduleItem(&titem, false) == true)
				m_scheduleitems.push_back(titem);
		}
	}
}

void CScheduler::SetSunRiseSetTimers(const std::string &sSunRise, const std::string &sSunSet, const std::string &sSunAtSouth, const std::string &sCivTwStart, const std::string &sCivTwEnd, const std::string &sNautTwStart, const std::string &sNautTwEnd, const std::string &sAstTwStart, const std::string &sAstTwEnd)
{
	bool bReloadSchedules = false;

	{	//needed private scope for the lock
		std::lock_guard<std::mutex> l(m_mutex);

		time_t temptime;
		time_t atime = mytime(NULL);
		struct tm ltime;
		localtime_r(&atime, &ltime);
		struct tm tm1;

		std::string  allSchedules[] = {sSunRise, sSunSet, sSunAtSouth, sCivTwStart, sCivTwEnd, sNautTwStart, sNautTwEnd, sAstTwStart, sAstTwEnd};
		time_t *allTimes[] = {&m_tSunRise, &m_tSunSet, &m_tSunAtSouth, &m_tCivTwStart, &m_tCivTwEnd, &m_tNautTwStart, &m_tNautTwEnd, &m_tAstTwStart, &m_tAstTwEnd};
		for(unsigned int a = 0; a < sizeof(allSchedules)/sizeof(allSchedules[0]); a = a + 1)
		{
			//std::cout << allSchedules[a].c_str() << ' ';
			int hour = atoi(allSchedules[a].substr(0, 2).c_str());
			int min = atoi(allSchedules[a].substr(3, 2).c_str());
			int sec = atoi(allSchedules[a].substr(6, 2).c_str());

			constructTime(temptime,tm1,ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday,hour,min,sec,ltime.tm_isdst);
			if ((*allTimes[a] != temptime) && (temptime != 0))
			{
				if (*allTimes[a] == 0)
					bReloadSchedules = true;
				*allTimes[a] = temptime;
			}
		}
	}

	if (bReloadSchedules)
		ReloadSchedules();
}

bool CScheduler::AdjustScheduleItem(tScheduleItem *pItem, bool bForceAddDay)
{
	time_t atime = mytime(NULL);
	time_t rtime = atime;
	struct tm ltime;
	localtime_r(&atime, &ltime);
	int isdst = ltime.tm_isdst;
	struct tm tm1;
	memset(&tm1, 0, sizeof(tm));
	tm1.tm_isdst = -1;

	if (bForceAddDay)
		ltime.tm_mday++;

	unsigned long HourMinuteOffset = (pItem->startHour * 3600) + (pItem->startMin * 60);

	int nRandomTimerFrame = 15;
	m_sql.GetPreferencesVar("RandomTimerFrame", nRandomTimerFrame);
	if (nRandomTimerFrame == 0)
		nRandomTimerFrame = 15;
	int roffset = 0;
	if (pItem->bUseRandomness)
	{
		if ((pItem->timerType == TTYPE_BEFORESUNRISE) ||
			(pItem->timerType == TTYPE_AFTERSUNRISE) ||
			(pItem->timerType == TTYPE_BEFORESUNSET) ||
			(pItem->timerType == TTYPE_AFTERSUNSET) ||

			(pItem->timerType == TTYPE_BEFORESUNATSOUTH) ||
			(pItem->timerType == TTYPE_AFTERSUNATSOUTH) ||
			(pItem->timerType == TTYPE_BEFORECIVTWSTART) ||
			(pItem->timerType == TTYPE_AFTERCIVTWSTART) ||
			(pItem->timerType == TTYPE_BEFORECIVTWEND) ||
			(pItem->timerType == TTYPE_AFTERCIVTWEND) ||
			(pItem->timerType == TTYPE_BEFORENAUTTWSTART) ||
			(pItem->timerType == TTYPE_AFTERNAUTTWSTART) ||
			(pItem->timerType == TTYPE_BEFORENAUTTWEND) ||
			(pItem->timerType == TTYPE_AFTERNAUTTWEND) ||
			(pItem->timerType == TTYPE_BEFOREASTTWSTART) ||
			(pItem->timerType == TTYPE_AFTERASTTWSTART) ||
			(pItem->timerType == TTYPE_BEFOREASTTWEND) ||
			(pItem->timerType == TTYPE_AFTERASTTWEND))
			roffset = rand() % (nRandomTimerFrame);
		else
			roffset = rand() % (nRandomTimerFrame * 2) - nRandomTimerFrame;
	}
	if ((pItem->timerType == TTYPE_ONTIME) ||
		(pItem->timerType == TTYPE_DAYSODD) ||
		(pItem->timerType == TTYPE_DAYSEVEN) ||
		(pItem->timerType == TTYPE_WEEKSODD) ||
		(pItem->timerType == TTYPE_WEEKSEVEN))
	{
		constructTime(rtime,tm1,ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday,pItem->startHour,pItem->startMin,roffset*60,isdst);
		while (rtime < atime + 60)
		{
			ltime.tm_mday++;
			constructTime(rtime,tm1,ltime.tm_year+1900,ltime.tm_mon+1,ltime.tm_mday,pItem->startHour,pItem->startMin,roffset*60,isdst);
		}
		pItem->startTime = rtime;
		return true;
	}
	else if (pItem->timerType == TTYPE_FIXEDDATETIME)
	{
		constructTime(rtime,tm1,pItem->startYear,pItem->startMonth,pItem->startDay,pItem->startHour,pItem->startMin,roffset*60,isdst);
		if (rtime < atime)
			return false; //past date/time
		pItem->startTime = rtime;
		return true;
	}
	else if (pItem->timerType == TTYPE_BEFORESUNSET)
	{
		if (m_tSunSet == 0)
			return false;
		rtime = m_tSunSet - HourMinuteOffset - (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_AFTERSUNSET)
	{
		if (m_tSunSet == 0)
			return false;
		rtime = m_tSunSet + HourMinuteOffset + (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_BEFORESUNRISE)
	{
		if (m_tSunRise == 0)
			return false;
		rtime = m_tSunRise - HourMinuteOffset - (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_AFTERSUNRISE)
	{
		if (m_tSunRise == 0)
			return false;
		rtime = m_tSunRise + HourMinuteOffset + (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_BEFORESUNATSOUTH)
	{
		if (m_tSunAtSouth == 0)
			return false;
		rtime = m_tSunAtSouth - HourMinuteOffset - (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_AFTERSUNATSOUTH)
	{
		if (m_tSunAtSouth == 0)
			return false;
		rtime = m_tSunAtSouth + HourMinuteOffset + (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_BEFORECIVTWSTART)
	{
		if (m_tCivTwStart == 0)
			return false;
		rtime = m_tCivTwStart - HourMinuteOffset - (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_AFTERCIVTWSTART)
	{
		if (m_tCivTwStart == 0)
			return false;
		rtime = m_tCivTwStart + HourMinuteOffset + (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_BEFORECIVTWEND)
	{
		if (m_tCivTwEnd == 0)
			return false;
		rtime = m_tCivTwEnd - HourMinuteOffset - (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_AFTERCIVTWEND)
	{
		if (m_tCivTwEnd == 0)
			return false;
		rtime = m_tCivTwEnd + HourMinuteOffset + (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_BEFORENAUTTWSTART)
	{
		if (m_tNautTwStart == 0)
			return false;
		rtime = m_tNautTwStart - HourMinuteOffset - (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_AFTERNAUTTWSTART)
	{
		if (m_tNautTwStart == 0)
			return false;
		rtime = m_tNautTwStart + HourMinuteOffset + (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_BEFORENAUTTWEND)
	{
		if (m_tNautTwEnd == 0)
			return false;
		rtime = m_tNautTwEnd - HourMinuteOffset - (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_AFTERNAUTTWEND)
	{
		if (m_tNautTwEnd == 0)
			return false;
		rtime = m_tNautTwEnd + HourMinuteOffset + (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_BEFOREASTTWSTART)
	{
		if (m_tAstTwStart == 0)
			return false;
		rtime = m_tAstTwStart - HourMinuteOffset - (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_AFTERASTTWSTART)
	{
		if (m_tAstTwStart == 0)
			return false;
		rtime = m_tAstTwStart + HourMinuteOffset + (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_BEFOREASTTWEND)
	{
		if (m_tAstTwEnd == 0)
			return false;
		rtime = m_tAstTwEnd - HourMinuteOffset - (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_AFTERASTTWEND)
	{
		if (m_tAstTwEnd == 0)
			return false;
		rtime = m_tAstTwEnd + HourMinuteOffset + (roffset * 60);
	}
	else if (pItem->timerType == TTYPE_MONTHLY)
	{
		constructTime(rtime,tm1,ltime.tm_year+1900,ltime.tm_mon+1,pItem->MDay,pItem->startHour,pItem->startMin,0,isdst);

		while ( (rtime < atime) || (tm1.tm_mday != pItem->MDay) ) // past date/time OR mday exceeds max days in month
		{
			ltime.tm_mon++;
			constructTime(rtime,tm1,ltime.tm_year+1900,ltime.tm_mon+1,pItem->MDay,pItem->startHour,pItem->startMin,0,isdst);
		}

		rtime += roffset * 60; // add randomness
		pItem->startTime = rtime;
		return true;
	}
	else if (pItem->timerType == TTYPE_MONTHLY_WD)
	{
		//pItem->Days: mon=1 .. sat=32, sun=64
		//convert to : sun=0, mon=1 .. sat=6
		int daynum = (int)log2(pItem->Days) + 1;
		if (daynum == 7) daynum = 0;

		boost::gregorian::nth_day_of_the_week_in_month::week_num Occurence = static_cast<boost::gregorian::nth_day_of_the_week_in_month::week_num>(pItem->Occurence);
		boost::gregorian::greg_weekday::weekday_enum Day = static_cast<boost::gregorian::greg_weekday::weekday_enum>(daynum);
		boost::gregorian::months_of_year Month = static_cast<boost::gregorian::months_of_year>(ltime.tm_mon + 1);

		typedef boost::gregorian::nth_day_of_the_week_in_month nth_dow;
		nth_dow ndm(Occurence, Day, Month);
		boost::gregorian::date d = ndm.get_date(ltime.tm_year + 1900);

		constructTime(rtime,tm1,ltime.tm_year+1900,ltime.tm_mon+1,d.day(),pItem->startHour,pItem->startMin,0,isdst);

		if (rtime < atime) //past date/time
		{
			//schedule for next month
			ltime.tm_mon++;
			if (ltime.tm_mon == 12) // fix for roll over to next year
			{
				ltime.tm_mon = 0;
				ltime.tm_year++;
			}
			Month = static_cast<boost::gregorian::months_of_year>(ltime.tm_mon + 1);
			nth_dow ndm(Occurence, Day, Month);
			boost::gregorian::date d = ndm.get_date(ltime.tm_year + 1900);

			constructTime(rtime,tm1,ltime.tm_year+1900,ltime.tm_mon,d.day(),pItem->startHour,pItem->startMin,0,isdst);
		}

		rtime += roffset * 60; // add randomness
		pItem->startTime = rtime;
		return true;
	}
	else if (pItem->timerType == TTYPE_YEARLY)
	{
		constructTime(rtime,tm1,ltime.tm_year+1900,pItem->Month,pItem->MDay,pItem->startHour,pItem->startMin,0,isdst);

		while ( (rtime < atime) || (tm1.tm_mday != pItem->MDay) ) // past date/time OR mday exceeds max days in month
		{
			//schedule for next year
			ltime.tm_year++;
			constructTime(rtime,tm1,ltime.tm_year+1900,pItem->Month,pItem->MDay,pItem->startHour,pItem->startMin,0,isdst);
		}

		rtime += roffset * 60; // add randomness
		pItem->startTime = rtime;
		return true;
	}
	else if (pItem->timerType == TTYPE_YEARLY_WD)
	{
		//pItem->Days: mon=1 .. sat=32, sun=64
		//convert to : sun=0, mon=1 .. sat=6
		int daynum = (int)log2(pItem->Days) + 1;
		if (daynum == 7) daynum = 0;

		boost::gregorian::nth_day_of_the_week_in_month::week_num Occurence = static_cast<boost::gregorian::nth_day_of_the_week_in_month::week_num>(pItem->Occurence);
		boost::gregorian::greg_weekday::weekday_enum Day = static_cast<boost::gregorian::greg_weekday::weekday_enum>(daynum);
		boost::gregorian::months_of_year Month = static_cast<boost::gregorian::months_of_year>(ltime.tm_mon + 1);

		typedef boost::gregorian::nth_day_of_the_week_in_month nth_dow;
		nth_dow ndm(Occurence, Day, Month);
		boost::gregorian::date d = ndm.get_date(ltime.tm_year + 1900);

		constructTime(rtime, tm1, ltime.tm_year + 1900, pItem->Month, d.day(), pItem->startHour, pItem->startMin, 0, isdst);

		if (rtime < atime) //past date/time
		{
			//schedule for next year
			ltime.tm_year++;
			constructTime(rtime, tm1, ltime.tm_year + 1900, pItem->Month, d.day(), pItem->startHour, pItem->startMin, 0, isdst);
		}

		rtime += roffset * 60; // add randomness
		pItem->startTime = rtime;
		return true;
	}
	else
		return false; //unknown timer type

	if (tm1.tm_isdst == -1) // rtime was loaded from sunset/sunrise values; need to initialize tm1
	{
		if (bForceAddDay) // Adjust timer by 1 day if item is scheduled for next day
			rtime += 86400;

		//FIXME: because we are referencing the wrong date for sunset/sunrise values (i.e. today)
		//	 it may lead to incorrect results if we use localtime_r for finding the correct time
		while (rtime < atime + 60)
		{
			rtime += 86400;
		}
		pItem->startTime = rtime;
		return true;
		// end of FIXME block

		//FIXME: keep for future reference
		//tm1.tm_isdst = isdst; // load our current DST value and allow localtime_r to correct our 'mistake'
		//localtime_r(&rtime, &tm1);
		//isdst = tm1.tm_isdst;
	}

	// Adjust timer by 1 day if we are in the past
	//FIXME: this part of the code currently seems impossible to reach, but I may be overseeing something. Therefore:
	//	 it should be noted that constructTime() can return a time where tm1.tm_hour is different from pItem->startHour
	//	 The main cause for this to happen is that startHour is not a valid time on that day due to changing to Summertime
	//	 in which case the hour will be incremented by 1. By using tm1.tm_hour here this adjustment will propagate into
	//	 whatever following day may result from this loop and cause the timer to be set 1 hour later than it should be.
	while (rtime < atime + 60)
	{
		tm1.tm_mday++;
		struct tm tm2;
		constructTime(rtime, tm2, tm1.tm_year + 1900, tm1.tm_mon + 1, tm1.tm_mday, tm1.tm_hour, tm1.tm_min, tm1.tm_sec, isdst);
	}

	pItem->startTime = rtime;
	return true;
}

void CScheduler::Do_Work()
{
	while (!IsStopRequested(1000))
	{
		time_t atime = mytime(NULL);
		struct tm ltime;
		localtime_r(&atime, &ltime);


		if (ltime.tm_sec % 12 == 0) {
			m_mainworker.HeartbeatUpdate("Scheduler");
		}

		CheckSchedules();

		if (ltime.tm_sec == 0) {
			DeleteExpiredTimers();
		}
	}
	_log.Log(LOG_STATUS, "Scheduler stopped...");
}

void CScheduler::CheckSchedules()
{
	std::lock_guard<std::mutex> l(m_mutex);

	time_t atime = mytime(NULL);
	struct tm ltime;
	localtime_r(&atime, &ltime);

	for (auto & itt : m_scheduleitems)
	{
		if ((itt.bEnabled) && (atime > itt.startTime))
		{
			//check if we are on a valid day
			bool bOkToFire = false;
			if (itt.timerType == TTYPE_FIXEDDATETIME)
			{
				bOkToFire = true;
			}
			else if (itt.timerType == TTYPE_DAYSODD)
			{
				bOkToFire = (ltime.tm_mday % 2 != 0);
			}
			else if (itt.timerType == TTYPE_DAYSEVEN)
			{
				bOkToFire = (ltime.tm_mday % 2 == 0);
			}
			else
			{
				if (itt.Days & 0x80)
				{
					//everyday
					bOkToFire = true;
				}
				else if (itt.Days & 0x100)
				{
					//weekdays
					if ((ltime.tm_wday > 0) && (ltime.tm_wday < 6))
						bOkToFire = true;
				}
				else if (itt.Days & 0x200)
				{
					//weekends
					if ((ltime.tm_wday == 0) || (ltime.tm_wday == 6))
						bOkToFire = true;
				}
				else
				{
					//custom days
					if ((itt.Days & 0x01) && (ltime.tm_wday == 1))
						bOkToFire = true;//Monday
					if ((itt.Days & 0x02) && (ltime.tm_wday == 2))
						bOkToFire = true;//Tuesday
					if ((itt.Days & 0x04) && (ltime.tm_wday == 3))
						bOkToFire = true;//Wednesday
					if ((itt.Days & 0x08) && (ltime.tm_wday == 4))
						bOkToFire = true;//Thursday
					if ((itt.Days & 0x10) && (ltime.tm_wday == 5))
						bOkToFire = true;//Friday
					if ((itt.Days & 0x20) && (ltime.tm_wday == 6))
						bOkToFire = true;//Saturday
					if ((itt.Days & 0x40) && (ltime.tm_wday == 0))
						bOkToFire = true;//Sunday
				}
				if (bOkToFire)
				{
					if ((itt.timerType == TTYPE_WEEKSODD) ||
						(itt.timerType == TTYPE_WEEKSEVEN))
					{
						struct tm timeinfo;
						localtime_r(&itt.startTime, &timeinfo);

						boost::gregorian::date d = boost::gregorian::date(
							timeinfo.tm_year + 1900,
							timeinfo.tm_mon + 1,
							timeinfo.tm_mday);
						int w = d.week_number();

						if (itt.timerType == TTYPE_WEEKSODD)
							bOkToFire = (w % 2 != 0);
						else
							bOkToFire = (w % 2 == 0);
					}
				}
			}
			if (bOkToFire)
			{
				char ltimeBuf[30];
				strftime(ltimeBuf, sizeof(ltimeBuf), "%Y-%m-%d %H:%M:%S", &ltime);

				if (itt.bIsScene == true)
					_log.Log(LOG_STATUS, "Schedule item started! Name: %s, Type: %s, SceneID: %" PRIu64 ", Time: %s", itt.DeviceName.c_str(), Timer_Type_Desc(itt.timerType), itt.RowID, ltimeBuf);
				else if (itt.bIsThermostat == true)
					_log.Log(LOG_STATUS, "Schedule item started! Name: %s, Type: %s, ThermostatID: %" PRIu64 ", Time: %s", itt.DeviceName.c_str(), Timer_Type_Desc(itt.timerType), itt.RowID, ltimeBuf);
				else
					_log.Log(LOG_STATUS, "Schedule item started! Name: %s, Type: %s, DevID: %" PRIu64 ", Time: %s", itt.DeviceName.c_str(), Timer_Type_Desc(itt.timerType), itt.RowID, ltimeBuf);
				std::string switchcmd = "";
				if (itt.timerCmd == 0)
				{
					switchcmd = "Off";
				}
				else if (itt.timerCmd > 0)
				{
					switchcmd = "On";
				}


				if (switchcmd == "")
				{
					_log.Log(LOG_ERROR, "Unknown switch command in timer!!....");
				}
				else
				{
					if (itt.bIsScene == true)
					{
						if (!m_mainworker.SwitchScene(itt.RowID, switchcmd, "timer"))
						{
							_log.Log(LOG_ERROR, "Error switching Scene command, SceneID: %" PRIu64 ", Time: %s", itt.RowID, ltimeBuf);
						}
					}
					else if (itt.bIsThermostat == true)
					{
						std::stringstream sstr;
						sstr << itt.RowID;
						if (!m_mainworker.SetSetPoint(sstr.str(), itt.Temperature))
						{
							_log.Log(LOG_ERROR, "Error setting thermostat setpoint, ThermostatID: %" PRIu64 ", Time: %s", itt.RowID, ltimeBuf);
						}
					}
					else
					{
						//Get SwitchType
						std::vector<std::vector<std::string> > result;
						result = m_sql.safe_query("SELECT Type,SubType,SwitchType FROM DeviceStatus WHERE (ID == %" PRIu64 ")",
							itt.RowID);
						if (!result.empty())
						{
							std::vector<std::string> sd = result[0];

							unsigned char dType = atoi(sd[0].c_str());
							unsigned char dSubType = atoi(sd[1].c_str());
							_eSwitchType switchtype = (_eSwitchType)atoi(sd[2].c_str());
							//std::string lstatus = "";
							int llevel = 0;
							bool bHaveDimmer = false;
							bool bHaveGroupCmd = false;
							int maxDimLevel = 0;
							std::vector<std::string> cmdList;
							std::string sValue = std::to_string(itt.Level);

							GetLightStatus(dType, dSubType, switchtype, itt.timerCmd, sValue, switchcmd, llevel, bHaveDimmer, maxDimLevel, bHaveGroupCmd);
							int ilevel = maxDimLevel;
							if ((switchtype == STYPE_BlindsPercentage) || (switchtype == STYPE_BlindsPercentageInverted))
							{
								if (switchcmd == "On")
								{
									switchcmd = "Set Level";
									float fLevel = (maxDimLevel / 100.0f)*itt.Level;
									if (fLevel > 100)
										fLevel = 100;
									ilevel = int(fLevel);
								}
								else if (switchcmd == "Off")
									ilevel = 0;
							}
							else if (switchtype == STYPE_Selector) {
								if (switchcmd == "Set Level") {
									ilevel = itt.Level;
								} else if (switchcmd == "Off") {
									ilevel = 0; // force level to a valid value for Selector
								}
							}
							else if (switchtype == STYPE_Dimmer)
							{
								float fLevel = (maxDimLevel / 100.0f) * itt.Level;
								if (fLevel > 100)
									fLevel = 100;
								ilevel = round(fLevel);

								_log.Log(LOG_NORM, "witchscene led lstatus: %s", switchcmd.c_str());
								if (switchcmd == "On")
								{
									cmdList.emplace_back("On");
									if (itt.Color.mode ==  ColorModeNone)
									{
										if (ilevel > 0)
										{
											cmdList.emplace_back("Set Level");
										}
									}
									else
									{
										cmdList.emplace_back("Set Color");
									}
								}
								else if (switchcmd.find("Set Level") != std::string::npos)
								{
									if (itt.Color.mode !=  ColorModeNone)
									{
										cmdList.emplace_back("Set Color");
									}
									else{
										cmdList.emplace_back("Set Level");
									}
								}
							}

							if (cmdList.empty())
							{
								cmdList.emplace_back(switchcmd);
							}

							for (const auto& oneCmd : cmdList)
							{
								if (!m_mainworker.SwitchLight(itt.RowID, oneCmd, ilevel, itt.Color, false, 0, "timer"))
								{
									_log.Log(LOG_ERROR, "Error sending switch command, DevID: %" PRIu64 ", Time: %s", itt.RowID, ltimeBuf);
								}
							}
						}
					}
				}
			}
			if (!AdjustScheduleItem(&itt, true))
			{
				//something is wrong, probably no sunset/rise
				if (itt.timerType != TTYPE_FIXEDDATETIME)
				{
					itt.startTime += atime + (24 * 3600);
				}
				else {
					//Disable timer
					itt.bEnabled = false;
				}
			}
		}
	}
}

void CScheduler::DeleteExpiredTimers()
{
	char szDate[40];
	char szTime[40];
	time_t now = mytime(NULL);
	struct tm tmnow;
	localtime_r(&now, &tmnow);
	sprintf(szDate, "%04d-%02d-%02d", tmnow.tm_year + 1900, tmnow.tm_mon + 1, tmnow.tm_mday);
	sprintf(szTime, "%02d:%02d", tmnow.tm_hour, tmnow.tm_min);
	int iExpiredTimers = 0;

	std::vector<std::vector<std::string> > result;
	// Check Timers
	result = m_sql.safe_query("SELECT ID FROM Timers WHERE (Type == %i AND ((Date < '%q') OR (Date == '%q' AND Time < '%q')))",
		TTYPE_FIXEDDATETIME,
		szDate,
		szDate,
		szTime
		);
	if (!result.empty()) {
		m_sql.safe_query("DELETE FROM Timers WHERE (Type == %i AND ((Date < '%q') OR (Date == '%q' AND Time < '%q')))",
			TTYPE_FIXEDDATETIME,
			szDate,
			szDate,
			szTime
			);
		iExpiredTimers += result.size();
	}

	// Check SceneTimers
	result = m_sql.safe_query("SELECT ID FROM SceneTimers WHERE (Type == %i AND ((Date < '%q') OR (Date == '%q' AND Time < '%q')))",
		TTYPE_FIXEDDATETIME,
		szDate,
		szDate,
		szTime
		);
	if (!result.empty()) {
		m_sql.safe_query("DELETE FROM SceneTimers WHERE (Type == %i AND ((Date < '%q') OR (Date == '%q' AND Time < '%q')))",
			TTYPE_FIXEDDATETIME,
			szDate,
			szDate,
			szTime
			);
		iExpiredTimers += result.size();
	}

	if (iExpiredTimers > 0) {
		_log.Log(LOG_STATUS, "Purged %i expired (scene)timer(s)", iExpiredTimers);

		ReloadSchedules();
	}
}

//Webserver helpers
namespace http {
	namespace server {
		void CWebServer::RType_Schedules(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string client = request::findValue(&req, "client");
			if (!client.empty()) {
				boost::to_lower(client);
			}

			std::string mac = request::findValue(&req, "mac");
			std::string outlet = request::findValue(&req, "outlet");
			std::vector<std::vector<std::string> > reqInfo;
			if (!client.empty() && client != "web" && !mac.empty() && !outlet.empty()) {
				reqInfo = ConverParamsPlus(req, false);
				if (reqInfo.empty())
				{
					return;
				}
			}

			std::string rfilter = request::findValue(&req, "filter");
			root["status"] = "OK";
			root["title"] = "Schedules";

			std::vector<std::vector<std::string> > tot_result;
			std::vector<std::vector<std::string> > tmp_result;

			//Retrieve active schedules
			std::vector<tScheduleItem> schedules = m_mainworker.m_scheduler.GetScheduleItems();

			//Add Timers
			if ((rfilter == "all") || (rfilter == "") || (rfilter == "device")) {
				tmp_result = m_sql.safe_query(
					"SELECT t.ID, t.Active, d.[Name], t.DeviceRowID AS ID, 0 AS IsScene, 0 AS IsThermostat,"
					" t.[Date], t.Time, t.Type, t.Cmd, t.Level, t.Color, 0 AS Temperature, t.Days,"
					" t.UseRandomness, t.MDay, t.Month, t.Occurence, d.Mac, d.Outlet"
					" FROM Timers AS t, DeviceStatus AS d"
					" WHERE (d.ID == t.DeviceRowID) AND (t.TimerPlan==%d)"
					" ORDER BY d.[Name], t.Time",
					m_sql.m_ActiveTimerPlan);
				if (tmp_result.size() > 0)
					tot_result.insert(tot_result.end(), tmp_result.begin(), tmp_result.end());
			}

			//Add Scene-Timers
			if ((rfilter == "all") || (rfilter == "") || (rfilter == "scene")) {
				tmp_result = m_sql.safe_query(
					"SELECT t.ID, t.Active, s.[Name], t.SceneRowID AS ID, 1 AS IsScene, 0 AS IsThermostat"
					", t.[Date], t.Time, t.Type, t.Cmd, t.Level, '' AS Color, 0 AS Temperature, t.Days,"
					" t.UseRandomness, t.MDay, t.Month, t.Occurence, '' AS Mac, 0 AS Outlet"
					" FROM SceneTimers AS t, Scenes AS s"
					" WHERE (s.ID == t.SceneRowID) AND (t.TimerPlan==%d)"
					" ORDER BY s.[Name], t.Time",
					m_sql.m_ActiveTimerPlan);
				if (tmp_result.size() > 0)
					tot_result.insert(tot_result.end(), tmp_result.begin(), tmp_result.end());
			}

			//Add Setpoint-Timers
			if ((rfilter == "all") || (rfilter == "") || (rfilter == "thermostat")) {
				tmp_result = m_sql.safe_query(
					"SELECT t.ID, t.Active, d.[Name], t.DeviceRowID AS ID, 0 AS IsScene, 1 AS IsThermostat,"
					" t.[Date], t.Time, t.Type, 0 AS Cmd, 0 AS Level, '' AS Color, t.Temperature, t.Days,"
					" 0 AS UseRandomness, t.MDay, t.Month, t.Occurence, d.Mac, d.Outlet"
					" FROM SetpointTimers AS t, DeviceStatus AS d"
					" WHERE (d.ID == t.DeviceRowID) AND (t.TimerPlan==%d)"
					" ORDER BY d.[Name], t.Time",
					m_sql.m_ActiveTimerPlan);
				if (tmp_result.size() > 0)
					tot_result.insert(tot_result.end(), tmp_result.begin(), tmp_result.end());
			}

			if (tot_result.size() > 0)
			{
				int ii = 0;
				for (const auto & itt : tot_result)
				{
					std::vector<std::string> sd = itt;

					int iTimerIdx = atoi(sd[0].c_str());
					bool bActive = atoi(sd[1].c_str()) != 0;
					bool bIsScene = atoi(sd[4].c_str()) != 0;
					bool bIsThermostat = atoi(sd[5].c_str()) != 0;
					int iDays = atoi(sd[13].c_str());
					char ltimeBuf[30] = "";

					if (bActive)
					{
						//Find Timer in active schedules and retrieve ScheduleDate
						tScheduleItem sitem;
						sitem.TimerID = iTimerIdx;
						sitem.bIsScene = bIsScene;
						sitem.bIsThermostat = bIsThermostat;
						std::vector<tScheduleItem>::iterator it = std::find(schedules.begin(), schedules.end(), sitem);
						if (it != schedules.end()) {
							struct tm timeinfo;
							localtime_r(&it->startTime, &timeinfo);
							strftime(ltimeBuf, sizeof(ltimeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
						}
					}

					unsigned char iLevel = atoi(sd[10].c_str());
					if (iLevel == 0)
						iLevel = 100;

					int iTimerType = atoi(sd[8].c_str());
					std::string sdate = sd[6];
					if ((iTimerType == TTYPE_FIXEDDATETIME) && (sdate.size() == 10))
					{
						char szTmp[30];
						int Year = atoi(sdate.substr(0, 4).c_str());
						int Month = atoi(sdate.substr(5, 2).c_str());
						int Day = atoi(sdate.substr(8, 2).c_str());
						sprintf(szTmp, "%04d-%02d-%02d", Year, Month, Day);
						sdate = szTmp;
					}
					else
						sdate = "";

					bool find  = true;
					if (!reqInfo.empty())
					{
						find  = false;
						for ( const auto& itt : reqInfo)
						{
							if (sd[3] == itt[0])
							{
								find = true;
							}
						}
					}
					if (!find)
					{
						_log.Debug(DEBUG_WEBSERVER, "The idx is not we want %s", sd[3].c_str());
						continue;
					}
					root["result"][ii]["TimerID"] = iTimerIdx;
					root["result"][ii]["Active"] = bActive ? "true" : "false";
					root["result"][ii]["Type"] = bIsScene ? "Scene" : "Device";
					root["result"][ii]["IsThermostat"] = bIsThermostat ? "true" : "false";
					root["result"][ii]["DevName"] = sd[2];
					if (true == bIsScene)
					{
						root["result"][ii]["DeviceRowID"] = atoi(sd[3].c_str()); //TODO: Isn't this a 64 bit device index?
					}
					else
					{
						root["result"][ii]["Mac"] = sd[18];
						root["result"][ii]["Outlet"] = atoi(sd[19].c_str());
					}

					root["result"][ii]["Date"] = sdate;
					root["result"][ii]["Time"] = sd[7];
					root["result"][ii]["TimerType"] = iTimerType;
					root["result"][ii]["TimerTypeStr"] = Timer_Type_Desc(iTimerType);
					root["result"][ii]["Days"] = iDays;
					root["result"][ii]["MDay"] = atoi(sd[15].c_str());
					root["result"][ii]["Month"] = atoi(sd[16].c_str());
					root["result"][ii]["Occurence"] = atoi(sd[17].c_str());
					root["result"][ii]["ScheduleDate"] = ltimeBuf;
					if (bIsThermostat)
					{
						root["result"][ii]["Temperature"] = sd[12];
					}
					else
					{
						root["result"][ii]["TimerCmd"] = atoi(sd[9].c_str());
						root["result"][ii]["Level"] = iLevel;
						root["result"][ii]["Color"] = sd[11];
						root["result"][ii]["Randomness"] = (atoi(sd[14].c_str()) != 0) ? "true" : "false";
					}
					ii++;
				}
			}
		}
		void CWebServer::RType_Timers(WebEmSession & session, const request& req, Json::Value &root)
		{
			uint64_t idx = 0;

			int tType = 0;
			std::string timerType;
			std::string client = request::findValue(&req, "client");
			if (!client.empty()) {
				boost::to_lower(client);
			}
			std::vector<std::string> idxList;
			std::vector<std::vector<std::string> > reqInfo;
			if (!client.empty() && client != "web") {
				reqInfo = ConverParamsPlus(req, false);
				if (reqInfo.empty())
				{
					return;
				}
				for (const auto& itt : reqInfo)
				{
					idxList.emplace_back(itt[0]);
				}
				timerType = request::findValue(&req, "timertype");
				if (!timerType.empty())
				{
					try{
						tType = std::stoi(timerType);
					}

					catch (const std::exception & e){
						_log.Log(LOG_ERROR, " timer Type num a number :%s", e.what());
						return;
					}
					timerType = "AND Type == " + std::to_string(tType);
				}
			}
			else
			{
				std::string idx = request::findValue(&req, "idx");
				if (idx.empty())
				{
					return;
				}
				idxList.emplace_back(idx);
			}

			root["status"] = "OK";
			root["title"] = "Timers";
			char szTmp[50];


			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID, Active, [Date], Time, Type, Cmd, Level, Color, Days, UseRandomness, MDay, Month, Occurence, DeviceRowID"
									  " FROM Timers WHERE (DeviceRowID IN (%q)) AND (TimerPlan==%d) %s ORDER BY ID",
										boost::join(idxList, ",").c_str(), m_sql.m_ActiveTimerPlan, timerType.c_str());
			if (!result.empty())
			{
				int ii = 0;
				for (const auto & itt : result)
				{
					std::vector<std::string> sd = itt;

					unsigned char iLevel = atoi(sd[6].c_str());
					if (iLevel == 0)
					{
						//iLevel = 100;
					}

					int iTimerType = atoi(sd[4].c_str());
					std::string sdate = sd[2];
					int Year = 0;
					int Month = 0;
					int Day = 0;
					if ((iTimerType == TTYPE_FIXEDDATETIME) && (sdate.size() == 10))
					{
						Year = atoi(sdate.substr(0, 4).c_str());
						Month = atoi(sdate.substr(5, 2).c_str());
						Day = atoi(sdate.substr(8, 2).c_str());
						sprintf(szTmp, "%04d-%02d-%02d", Year, Month, Day);
						sdate = szTmp;
					}
					else
						sdate = "";

					int active = atoi(sd[1].c_str());

					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Active"] = (active == 0) ? "false" : "true";
					root["result"][ii]["Date"] = sdate;
					root["result"][ii]["Time"] = sd[3].substr(0, 5);
					root["result"][ii]["Type"] = iTimerType;
					root["result"][ii]["Cmd"] = "Off";
					root["result"][ii]["Level"] = iLevel;
					root["result"][ii]["Color"] = sd[7];
					root["result"][ii]["Days"] = atoi(sd[8].c_str());
					root["result"][ii]["Randomness"] = (atoi(sd[9].c_str()) == 0) ? "false" : "true";
					root["result"][ii]["MDay"] = atoi(sd[10].c_str());
					root["result"][ii]["Month"] = atoi(sd[11].c_str());
					root["result"][ii]["Occurence"] = atoi(sd[12].c_str());

					int command = atoi(sd[5].c_str());
					std::string sValue = sd[6];
					std::string strCmd;
					//Get SwitchType
					auto device = m_sql.safe_query("SELECT Type,SubType,SwitchType FROM DeviceStatus WHERE (ID == %q)", sd[13].c_str());
					if (!device.empty())
					{
						int devType = atoi(device[0][0].c_str());
						int devSubType = atoi(device[0][1].c_str());
						_eSwitchType switchtype = (_eSwitchType)atoi(device[0][2].c_str());
						int llevel = 0;
						bool bHaveDimmer = false;
						bool bHaveGroupCmd = false;
						int maxDimLevel = 0;
						GetLightStatus(devType, devSubType, switchtype, command, sValue, strCmd, llevel, bHaveDimmer, maxDimLevel, bHaveGroupCmd);
						root["result"][ii]["Cmd"] = strCmd;
					}

					/* get the fixeddate time gaps */
					if (active != 0 && iTimerType == TTYPE_FIXEDDATETIME)
					{
						time_t now;
						time_t point;
						struct tm tm1;
						struct tm tm2;
						int hour = 0;
						int min = 0;
						now = mytime (NULL);
						localtime_r (&now, &tm1);
						if (!sdate.empty())
						{
							tm1.tm_year = Year;
							tm1.tm_mon = Month;
							tm1.tm_mday = Day;
						}
						if (!sd[3].empty())
						{
							sscanf (sd[3].c_str(), "%d:%d", &hour, &min);
						}

						tm1.tm_hour = hour;
						tm1.tm_min = min;
						constructTime(point, tm2,
										tm1.tm_year, tm1.tm_mon,tm1.tm_mday,
										tm1.tm_hour, tm1.tm_min, 0, tm1.tm_isdst);

						_log.Log(LOG_STATUS, "timer  %04d-%02d-%02d  %02d:%02d", Year, Month, Day, hour, min);
						char strTime[16];
						int gaps = point - now;
						memset(strTime, 0, sizeof(strTime));
						if (gaps >= 0)
						{
							tm1.tm_hour = 0;
							tm1.tm_min = 0;
							tm1.tm_sec = gaps;
							constructTime(point, tm2,
											tm1.tm_year, tm1.tm_mon, tm1.tm_mday,
											tm1.tm_hour, tm1.tm_min, tm1.tm_sec, tm1.tm_isdst);
							strftime(strTime, sizeof(strTime), "%H:%M:%S", &tm2);

							_log.Log(LOG_STATUS, "timer how much minute need to passes %s", strTime);
						}
						else
						{
							strcpy (strTime, "00:00:00");
						}
						root["result"][ii]["gaps"] = strTime;
					}


					for (const auto& info : reqInfo)
					{
						if (sd[13] == info[0])
						{
							root["result"][ii]["Mac"] = info[1];
							root["result"][ii]["Outlet"] = atoi(info[2].c_str());
						}
					}
					ii++;
				}
			}
		}

		void CWebServer::Cmd_SetActiveTimerPlan(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			int rnOldvalue = 0;
			int rnvalue = 0;
			m_sql.GetPreferencesVar("ActiveTimerPlan", rnOldvalue);
			rnvalue = atoi(request::findValue(&req, "ActiveTimerPlan").c_str());
			if (rnOldvalue != rnvalue)
			{
				std::vector<std::vector<std::string> > result;
				result = m_sql.safe_query("SELECT Name FROM TimerPlans WHERE (ID == %d)", rnvalue);
				if (result.empty())
					return; //timerplan not found!
				_log.Log(LOG_STATUS, "Scheduler Timerplan changed (%d - %s)", rnvalue, result[0][0].c_str());
				m_sql.UpdatePreferencesVar("ActiveTimerPlan", rnvalue);
				m_sql.m_ActiveTimerPlan = rnvalue;
				m_mainworker.m_scheduler.ReloadSchedules();
			}

			root["status"] = "OK";
			root["title"] = "SetActiveTimerPlan";
		}

		void CWebServer::Cmd_AddTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string client = request::findValue(&req, "client");
			if (!client.empty()) {
				boost::to_lower(client);
			}

			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			bool isApp = false;
			std::string idx;
			if (!client.empty() && client != "web") {
				idx = ConverParams(req, false);
				isApp = true;
			} else {
				idx = request::findValue(&req, "idx");
			}
			std::string active = request::findValue(&req, "active");
			std::string stimertype = request::findValue(&req, "timertype");
			std::string sdate = request::findValue(&req, "date");
			std::string shour = request::findValue(&req, "hour");
			std::string smin = request::findValue(&req, "min");
			std::string randomness = request::findValue(&req, "randomness");
			std::string scmd = request::findValue(&req, "command");
			std::string sdays = request::findValue(&req, "days");
			std::string slevel = request::findValue(&req, "level");	//in percentage
			std::string scolor = request::findValue(&req, "color");
			std::string smday = "0";
			std::string smonth = "0";
			std::string soccurence = "0";
			if (
				(idx == "") ||
				(active == "") ||
				(stimertype == "") ||
				(shour == "") ||
				(smin == "") ||
				(randomness == "") ||
				(scmd == "") ||
				(sdays == "")
				)
				return;
			unsigned char iTimerType = atoi(stimertype.c_str());

			time_t now = mytime(NULL);
			struct tm tm1;
			localtime_r(&now, &tm1);
			int Year = tm1.tm_year + 1900;
			int Month = tm1.tm_mon + 1;
			int Day = tm1.tm_mday;

			if (false == isApp && iTimerType == TTYPE_FIXEDDATETIME)
			{
				if (sdate.size() != 10)
				{
					_log.Log(LOG_ERROR, "The fixed date: %s", sdate.c_str());
					return;
				}

				Year = atoi(sdate.substr(0, 4).c_str());
				Month = atoi(sdate.substr(5, 2).c_str());
				Day = atoi(sdate.substr(8, 2).c_str());
			}
			else if (iTimerType == TTYPE_MONTHLY)
			{
				smday = request::findValue(&req, "mday");
				if (smday == "") return;
			}
			else if (iTimerType == TTYPE_MONTHLY_WD)
			{
				soccurence = request::findValue(&req, "occurence");
				if (soccurence == "") return;
			}
			else if (iTimerType == TTYPE_YEARLY)
			{
				smday = request::findValue(&req, "mday");
				smonth = request::findValue(&req, "month");
				if ((smday == "") || (smonth == "")) return;
			}
			else if (iTimerType == TTYPE_YEARLY_WD)
			{
				smonth = request::findValue(&req, "month");
				soccurence = request::findValue(&req, "occurence");
				if ((smonth == "") || (soccurence == "")) return;
			}

			unsigned char hour = atoi(shour.c_str());
			unsigned char min = atoi(smin.c_str());
			unsigned char icmd  = 0;
			int days = atoi(sdays.c_str());
			unsigned char level = atoi(slevel.c_str());
			_tColor color = _tColor(scolor);
			int mday = atoi(smday.c_str());
			int month = atoi(smonth.c_str());
			int occurence = atoi(soccurence.c_str());

			if (hour >= 24 || min >= 60)
			{
				_log.Log(LOG_ERROR, "AddTimer:the hour or min invaild, hour:%s min:%s", shour.c_str(), smin.c_str());
				return;
			}
			if (true == isApp && iTimerType == TTYPE_FIXEDDATETIME)
			{
				tm1.tm_hour += hour;
				tm1.tm_min += min;
				struct tm tm2;
				time_t time2;
				constructTime(time2, tm2,
							  tm1.tm_year+1900, tm1.tm_mon+1,tm1.tm_mday,
							  tm1.tm_hour, tm1.tm_min, 0, tm1.tm_isdst);
				Year = tm2.tm_year + 1900;
				Month = tm2.tm_mon + 1;
				Day =  tm2.tm_mday;
				hour = tm2.tm_hour;
				min = tm2.tm_min;
				_log.Log(LOG_STATUS, "add %04d-%02d-%02d  %02d:%02d", Year, Month, Day, hour, min);
			}


			if (isApp)
			{
				auto result = m_sql.safe_query("SELECT HardwareID, DeviceID, Unit, Type, SubType, SwitchType, Options FROM DeviceStatus WHERE (ID=='%q')",
					idx.c_str());
				if (result.empty())
				{
					_log.Log(LOG_ERROR, "AddTimer:get Device status failed at idx:%s", idx.c_str());
					return;
				}
				int dType = atoi(result[0][3].c_str());
				int sType = atoi(result[0][4].c_str());
				_eSwitchType switchtype = (_eSwitchType)atoi(result[0][5].c_str());
				std::map<std::string, std::string> options = m_sql.BuildDeviceOptions(result[0][6].c_str());
				bool res = GetLightCommand(dType, sType, switchtype, scmd, icmd, options);
				if (false == res)
				{
					_log.Log(LOG_ERROR, "AddTimer:get light command failed, plaese check the cmd: %s, device type:%d  subtype:%d switchtype:%d",
											scmd.c_str(), dType, sType, (int)switchtype);
					return;
				}
			}
			else
			{
				/* yp change the timer cmd */
				int tmp = atoi(scmd.c_str());
				icmd = (tmp == 0)? TCMD_ON:TCMD_OFF;
			}

			root["status"] = "OK";
			root["title"] = "AddTimer";
			m_sql.safe_query(
				"INSERT INTO Timers (Active, DeviceRowID, [Date], Time, Type, UseRandomness, Cmd, Level, Color, Days, MDay, Month, Occurence, TimerPlan) VALUES (%d,'%q','%04d-%02d-%02d','%02d:%02d',%d,%d,%d,%d,'%q',%d,%d,%d,%d,%d)",
				(active == "true") ? 1 : 0,
				idx.c_str(),
				Year, Month, Day,
				hour, min,
				iTimerType,
				(randomness == "true") ? 1 : 0,
				icmd,
				level,
				color.toJSONString().c_str(),
				days,
				mday,
				month,
				occurence,
				m_sql.m_ActiveTimerPlan
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_UpdateTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string client = request::findValue(&req, "client");
			if (!client.empty()) {
				boost::to_lower(client);
			}

			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			bool isApp = false;
			std::string idx;
			std::string devIdx;
			if (!client.empty() && client != "web") {
				isApp = true;
				devIdx = ConverParams(req, false);
			}

			idx = request::findValue(&req, "idx");

			std::string active = request::findValue(&req, "active");
			std::string stimertype = request::findValue(&req, "timertype");
			std::string sdate = request::findValue(&req, "date");
			std::string shour = request::findValue(&req, "hour");
			std::string smin = request::findValue(&req, "min");
			std::string randomness = request::findValue(&req, "randomness");
			std::string scmd = request::findValue(&req, "command");
			std::string sdays = request::findValue(&req, "days");
			std::string slevel = request::findValue(&req, "level");	//in percentage
			std::string scolor = request::findValue(&req, "color");
			std::string smday = "0";
			std::string smonth = "0";
			std::string soccurence = "0";
			if (
				(idx == "") ||
				(active == "") ||
				(stimertype == "") ||
				(shour == "") ||
				(smin == "") ||
				(randomness == "") ||
				(scmd == "") ||
				(sdays == "")
				)
				return;

			unsigned char iTimerType = atoi(stimertype.c_str());
			time_t now = mytime(NULL);
			struct tm tm1;
			localtime_r(&now, &tm1);
			int Year = tm1.tm_year + 1900;
			int Month = tm1.tm_mon + 1;
			int Day = tm1.tm_mday;

			if (false == isApp && iTimerType == TTYPE_FIXEDDATETIME)
			{
				if (sdate.size() == 10)
				{
					Year = atoi(sdate.substr(0, 4).c_str());
					Month = atoi(sdate.substr(5, 2).c_str());
					Day = atoi(sdate.substr(8, 2).c_str());
				}
			}
			else if (iTimerType == TTYPE_MONTHLY)
			{
				smday = request::findValue(&req, "mday");
				if (smday == "") return;
			}
			else if (iTimerType == TTYPE_MONTHLY_WD)
			{
				soccurence = request::findValue(&req, "occurence");
				if (soccurence == "") return;
			}
			else if (iTimerType == TTYPE_YEARLY)
			{
				smday = request::findValue(&req, "mday");
				smonth = request::findValue(&req, "month");
				if ((smday == "") || (smonth == "")) return;
			}
			else if (iTimerType == TTYPE_YEARLY_WD)
			{
				smonth = request::findValue(&req, "month");
				soccurence = request::findValue(&req, "occurence");
				if ((smonth == "") || (soccurence == "")) return;
			}

			unsigned char hour = atoi(shour.c_str());
			unsigned char min = atoi(smin.c_str());
			unsigned char icmd = 0;
			int days = atoi(sdays.c_str());
			unsigned char level = atoi(slevel.c_str());
			_tColor color = _tColor(scolor);
			int mday = atoi(smday.c_str());
			int month = atoi(smonth.c_str());
			int occurence = atoi(soccurence.c_str());

			if (hour >= 24 || min >= 60)
			{
				_log.Log(LOG_ERROR, "AddTimer:the hour or min invaild, hour:%s min:%s", shour.c_str(), smin.c_str());
				return;
			}

			if (true == isApp && iTimerType == TTYPE_FIXEDDATETIME)
			{
				tm1.tm_hour += hour;
				tm1.tm_min += min;
				struct tm tm2;
				time_t time2;
				constructTime(time2, tm2,
							  tm1.tm_year+1900, tm1.tm_mon+1, tm1.tm_mday,
							  tm1.tm_hour, tm1.tm_min, 0, tm1.tm_isdst);
				Year = tm2.tm_year + 1900;
				Month = tm2.tm_mon + 1;
				Day =  tm2.tm_mday;
				hour = tm2.tm_hour;
				min = tm2.tm_min;
				_log.Log(LOG_STATUS, "update %04d-%02d-%02d  %02d:%02d", Year, Month, Day, hour, min);
			}

			std::vector<std::vector<std::string>> result;
			if (devIdx.empty())
			{
				result =  m_sql.safe_query("select DeviceRowID from Timers where ID == %q", idx.c_str());
				if (result.empty())
				{
					_log.Log(LOG_ERROR, "UpdateTimer:get Device idx failed at timer idx:%s", idx.c_str());
					return;
				}
				devIdx = result[0][0];
			}

			if (isApp)
			{
				result = m_sql.safe_query("SELECT HardwareID, DeviceID, Unit, Type, SubType, SwitchType, Options FROM DeviceStatus WHERE (ID=='%q')",
					devIdx.c_str());
				if (result.empty())
				{
					_log.Log(LOG_ERROR, "UpdateTimer:get Device status failed at device idx:%s", devIdx.c_str());
					return;
				}

				int dType = atoi(result[0][3].c_str());
				int sType = atoi(result[0][4].c_str());
				_eSwitchType switchtype = (_eSwitchType)atoi(result[0][5].c_str());
				std::map<std::string, std::string> options = m_sql.BuildDeviceOptions(result[0][6].c_str());
				bool res = GetLightCommand(dType, sType, switchtype, scmd, icmd, options);
				if (false == res)
				{
					_log.Log(LOG_ERROR, "UpdateTimer:get light command failed, plaese check the cmd: %s, device type:%d  subtype:%d switchtype:%d",
											scmd.c_str(), dType, sType, (int)switchtype);
					return;
				}

			}
			else
			{
				/* yp change the timer cmd */
				int tmp = atoi(scmd.c_str());
				icmd = (tmp == 0)? TCMD_ON:TCMD_OFF;
			}

			root["status"] = "OK";
			root["title"] = "UpdateTimer";
			m_sql.safe_query(
				"UPDATE Timers SET DeviceRowID=%q, Active=%d, [Date]='%04d-%02d-%02d', Time='%02d:%02d', Type=%d, UseRandomness=%d, Cmd=%d, Level=%d, Color='%q', Days=%d, MDay=%d, Month=%d, Occurence=%d WHERE (ID == '%q')",
				devIdx.c_str(),
				(active == "true") ? 1 : 0,
				Year, Month, Day,
				hour, min,
				iTimerType,
				(randomness == "true") ? 1 : 0,
				icmd,
				level,
				color.toJSONString().c_str(),
				days,
				mday,
				month,
				occurence,
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_DeleteTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			std::vector<std::string> idxList;
			if (idx.empty())
			{
				return;
			}
			boost::split(idxList, idx, boost::is_any_of(";"), boost::token_compress_on);

			root["status"] = "OK";
			root["title"] = "DeleteTimer";
			for (const auto& itt : idxList)
			{
				m_sql.safe_query(
					"DELETE FROM Timers WHERE (ID == '%q')",
					itt.c_str()
					);
			}
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_EnableTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx == "")
				return;
			root["status"] = "OK";
			root["title"] = "EnableTimer";
			m_sql.safe_query(
				"UPDATE Timers SET Active=1 WHERE (ID == '%q')",
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_DisableTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx == "")
				return;
			root["status"] = "OK";
			root["title"] = "DisableTimer";
			m_sql.safe_query(
				"UPDATE Timers SET Active=0 WHERE (ID == '%q')",
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_ClearTimers(WebEmSession & session, const request& req, Json::Value &root)
		{
			std::string client = request::findValue(&req, "client");
			if (!client.empty()) {
				boost::to_lower(client);
			}

			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::vector<std::string> idxList;
			std::vector<std::vector<std::string> > reqInfo;
			if (!client.empty() && client != "web") {
				reqInfo = ConverParamsPlus(req, false);
				if (reqInfo.empty())
				{
					_log.Debug( DEBUG_WEBSERVER, "Reqeust info empty");
					return;
				}
				for (const auto& itt : reqInfo)
				{
					idxList.emplace_back(itt[0]);
				}

			} else {
				std::string idx = request::findValue(&req, "idx");
				if (idx == "")
				{
					return;
				}
				idxList.emplace_back(idx);
			}

			root["status"] = "OK";
			root["title"] = "ClearTimer";
			for (const auto& itt : idxList)
			{
				m_sql.safe_query(
					"DELETE FROM Timers WHERE ((DeviceRowID == '%q') AND (TimerPlan == %d))",
					itt.c_str(),
					m_sql.m_ActiveTimerPlan
					);
			}

			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::RType_SetpointTimers(WebEmSession & session, const request& req, Json::Value &root)
		{
			uint64_t idx = 0;
			if (request::findValue(&req, "idx") != "")
			{
				idx = std::strtoull(request::findValue(&req, "idx").c_str(), nullptr, 10);
			}
			if (idx == 0)
				return;
			root["status"] = "OK";
			root["title"] = "SetpointTimers";
			char szTmp[50];

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID, Active, [Date], Time, Type, Temperature, Days, MDay, Month, Occurence FROM SetpointTimers WHERE (DeviceRowID=%" PRIu64 ") AND (TimerPlan==%d) ORDER BY ID",
				idx, m_sql.m_ActiveTimerPlan);
			if (!result.empty())
			{
				int ii = 0;
				for (const auto & itt : result)
				{
					std::vector<std::string> sd = itt;

					int iTimerType = atoi(sd[4].c_str());
					std::string sdate = sd[2];
					if ((iTimerType == TTYPE_FIXEDDATETIME) && (sdate.size() == 10))
					{
						int Year = atoi(sdate.substr(0, 4).c_str());
						int Month = atoi(sdate.substr(5, 2).c_str());
						int Day = atoi(sdate.substr(8, 2).c_str());
						sprintf(szTmp, "%04d-%02d-%02d", Year, Month, Day);
						sdate = szTmp;
					}
					else
						sdate = "";

					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Active"] = (atoi(sd[1].c_str()) == 0) ? "false" : "true";
					root["result"][ii]["Date"] = sdate;
					root["result"][ii]["Time"] = sd[3].substr(0, 5);
					root["result"][ii]["Type"] = iTimerType;
					root["result"][ii]["Temperature"] = atof(sd[5].c_str());
					root["result"][ii]["Days"] = atoi(sd[6].c_str());
					root["result"][ii]["MDay"] = atoi(sd[7].c_str());
					root["result"][ii]["Month"] = atoi(sd[8].c_str());
					root["result"][ii]["Occurence"] = atoi(sd[9].c_str());
					ii++;
				}
			}
		}
		void CWebServer::Cmd_AddSetpointTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			std::string active = request::findValue(&req, "active");
			std::string stimertype = request::findValue(&req, "timertype");
			std::string sdate = request::findValue(&req, "date");
			std::string shour = request::findValue(&req, "hour");
			std::string smin = request::findValue(&req, "min");
			std::string stvalue = request::findValue(&req, "tvalue");
			std::string sdays = request::findValue(&req, "days");
			std::string smday = "0";
			std::string smonth = "0";
			std::string soccurence = "0";
			if (
				(idx == "") ||
				(active == "") ||
				(stimertype == "") ||
				(shour == "") ||
				(smin == "") ||
				(stvalue == "") ||
				(sdays == "")
				)
				return;
			unsigned char iTimerType = atoi(stimertype.c_str());

			time_t now = mytime(NULL);
			struct tm tm1;
			localtime_r(&now, &tm1);
			int Year = tm1.tm_year + 1900;
			int Month = tm1.tm_mon + 1;
			int Day = tm1.tm_mday;

			if (iTimerType == TTYPE_FIXEDDATETIME)
			{
				if (sdate.size() == 10)
				{
					Year = atoi(sdate.substr(0, 4).c_str());
					Month = atoi(sdate.substr(5, 2).c_str());
					Day = atoi(sdate.substr(8, 2).c_str());
				}
			}
			else if (iTimerType == TTYPE_MONTHLY)
			{
				smday = request::findValue(&req, "mday");
				if (smday == "") return;
			}
			else if (iTimerType == TTYPE_MONTHLY_WD)
			{
				soccurence = request::findValue(&req, "occurence");
				if (soccurence == "") return;
			}
			else if (iTimerType == TTYPE_YEARLY)
			{
				smday = request::findValue(&req, "mday");
				smonth = request::findValue(&req, "month");
				if ((smday == "") || (smonth == "")) return;
			}
			else if (iTimerType == TTYPE_YEARLY_WD)
			{
				smonth = request::findValue(&req, "month");
				soccurence = request::findValue(&req, "occurence");
				if ((smonth == "") || (soccurence == "")) return;
			}

			unsigned char hour = atoi(shour.c_str());
			unsigned char min = atoi(smin.c_str());
			int days = atoi(sdays.c_str());
			float temperature = static_cast<float>(atof(stvalue.c_str()));
			int mday = atoi(smday.c_str());
			int month = atoi(smonth.c_str());
			int occurence = atoi(soccurence.c_str());
			root["status"] = "OK";
			root["title"] = "AddSetpointTimer";
			m_sql.safe_query(
				"INSERT INTO SetpointTimers (Active, DeviceRowID, [Date], Time, Type, Temperature, Days, MDay, Month, Occurence, TimerPlan) VALUES (%d,'%q','%04d-%02d-%02d','%02d:%02d',%d,%.1f,%d,%d,%d,%d,%d)",
				(active == "true") ? 1 : 0,
				idx.c_str(),
				Year, Month, Day,
				hour, min,
				iTimerType,
				temperature,
				days,
				mday,
				month,
				occurence,
				m_sql.m_ActiveTimerPlan
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_UpdateSetpointTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			std::string active = request::findValue(&req, "active");
			std::string stimertype = request::findValue(&req, "timertype");
			std::string sdate = request::findValue(&req, "date");
			std::string shour = request::findValue(&req, "hour");
			std::string smin = request::findValue(&req, "min");
			std::string stvalue = request::findValue(&req, "tvalue");
			std::string sdays = request::findValue(&req, "days");
			std::string smday = "0";
			std::string smonth = "0";
			std::string soccurence = "0";
			if (
				(idx == "") ||
				(active == "") ||
				(stimertype == "") ||
				(shour == "") ||
				(smin == "") ||
				(stvalue == "") ||
				(sdays == "")
				)
				return;

			unsigned char iTimerType = atoi(stimertype.c_str());
			time_t now = mytime(NULL);
			struct tm tm1;
			localtime_r(&now, &tm1);
			int Year = tm1.tm_year + 1900;
			int Month = tm1.tm_mon + 1;
			int Day = tm1.tm_mday;

			if (iTimerType == TTYPE_FIXEDDATETIME)
			{
				if (sdate.size() == 10)
				{
					Year = atoi(sdate.substr(0, 4).c_str());
					Month = atoi(sdate.substr(5, 2).c_str());
					Day = atoi(sdate.substr(8, 2).c_str());
				}
			}
			else if (iTimerType == TTYPE_MONTHLY)
			{
				smday = request::findValue(&req, "mday");
				if (smday == "") return;
			}
			else if (iTimerType == TTYPE_MONTHLY_WD)
			{
				soccurence = request::findValue(&req, "occurence");
				if (soccurence == "") return;
			}
			else if (iTimerType == TTYPE_YEARLY)
			{
				smday = request::findValue(&req, "mday");
				smonth = request::findValue(&req, "month");
				if ((smday == "") || (smonth == "")) return;
			}
			else if (iTimerType == TTYPE_YEARLY_WD)
			{
				smonth = request::findValue(&req, "month");
				soccurence = request::findValue(&req, "occurence");
				if ((smonth == "") || (soccurence == "")) return;
			}

			unsigned char hour = atoi(shour.c_str());
			unsigned char min = atoi(smin.c_str());
			int days = atoi(sdays.c_str());
			float tempvalue = static_cast<float>(atof(stvalue.c_str()));
			int mday = atoi(smday.c_str());
			int month = atoi(smonth.c_str());
			int occurence = atoi(soccurence.c_str());
			root["status"] = "OK";
			root["title"] = "UpdateSetpointTimer";
			m_sql.safe_query(
				"UPDATE SetpointTimers SET Active=%d, [Date]='%04d-%02d-%02d', Time='%02d:%02d', Type=%d, Temperature=%.1f, Days=%d, MDay=%d, Month=%d, Occurence=%d WHERE (ID == '%q')",
				(active == "true") ? 1 : 0,
				Year, Month, Day,
				hour, min,
				iTimerType,
				tempvalue,
				days,
				mday,
				month,
				occurence,
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_DeleteSetpointTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx == "")
				return;
			root["status"] = "OK";
			root["title"] = "DeleteSetpointTimer";
			m_sql.safe_query(
				"DELETE FROM SetpointTimers WHERE (ID == '%q')",
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_EnableSetpointTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx == "")
				return;
			root["status"] = "OK";
			root["title"] = "EnableSetpointTimer";
			m_sql.safe_query(
				"UPDATE SetpointTimers SET Active=1 WHERE (ID == '%q')",
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_DisableSetpointTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx == "")
				return;
			root["status"] = "OK";
			root["title"] = "DisableSetpointTimer";
			m_sql.safe_query(
				"UPDATE SetpointTimers SET Active=0 WHERE (ID == '%q')",
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_ClearSetpointTimers(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx == "")
				return;
			root["status"] = "OK";
			root["title"] = "ClearSetpointTimers";
			m_sql.safe_query(
				"DELETE FROM SetpointTimers WHERE (DeviceRowID == '%q')",
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::RType_SceneTimers(WebEmSession & session, const request& req, Json::Value &root)
		{
			uint64_t idx = 0;
			if (request::findValue(&req, "idx") != "")
			{
				idx = std::strtoull(request::findValue(&req, "idx").c_str(), nullptr, 10);
			}
			if (idx == 0)
				return;
			root["status"] = "OK";
			root["title"] = "SceneTimers";

			char szTmp[40];

			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID, Active, [Date], Time, Type, Cmd, Level, Days, UseRandomness, MDay, Month, Occurence FROM SceneTimers WHERE (SceneRowID==%" PRIu64 ") AND (TimerPlan==%d) ORDER BY ID",
				idx, m_sql.m_ActiveTimerPlan);
			if (!result.empty())
			{
				int ii = 0;
				for (const auto & itt : result)
				{
					std::vector<std::string> sd = itt;

					unsigned char iLevel = atoi(sd[6].c_str());
					if (iLevel == 0)
						iLevel = 100;

					int iTimerType = atoi(sd[4].c_str());
					std::string sdate = sd[2];
					if ((iTimerType == TTYPE_FIXEDDATETIME) && (sdate.size() == 10))
					{
						int Year = atoi(sdate.substr(0, 4).c_str());
						int Month = atoi(sdate.substr(5, 2).c_str());
						int Day = atoi(sdate.substr(8, 2).c_str());
						sprintf(szTmp, "%04d-%02d-%02d", Year, Month, Day);
						sdate = szTmp;
					}
					else
						sdate = "";

					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Active"] = (atoi(sd[1].c_str()) == 0) ? "false" : "true";
					root["result"][ii]["Date"] = sdate;
					root["result"][ii]["Time"] = sd[3].substr(0, 5);
					root["result"][ii]["Type"] = iTimerType;
					root["result"][ii]["Cmd"] = atoi(sd[5].c_str());
					root["result"][ii]["Level"] = iLevel;
					root["result"][ii]["Days"] = atoi(sd[7].c_str());
					root["result"][ii]["Randomness"] = (atoi(sd[8].c_str()) == 0) ? "false" : "true";
					root["result"][ii]["MDay"] = atoi(sd[9].c_str());
					root["result"][ii]["Month"] = atoi(sd[10].c_str());
					root["result"][ii]["Occurence"] = atoi(sd[11].c_str());
					ii++;
				}
			}
		}

		void CWebServer::Cmd_AddSceneTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			std::string active = request::findValue(&req, "active");
			std::string stimertype = request::findValue(&req, "timertype");
			std::string sdate = request::findValue(&req, "date");
			std::string shour = request::findValue(&req, "hour");
			std::string smin = request::findValue(&req, "min");
			std::string randomness = request::findValue(&req, "randomness");
			std::string scmd = request::findValue(&req, "command");
			std::string sdays = request::findValue(&req, "days");
			std::string slevel = request::findValue(&req, "level");	//in percentage
			std::string smday = "0";
			std::string smonth = "0";
			std::string soccurence = "0";
			if (
				(idx == "") ||
				(active == "") ||
				(stimertype == "") ||
				(shour == "") ||
				(smin == "") ||
				(randomness == "") ||
				(scmd == "") ||
				(sdays == "")
				)
				return;
			unsigned char iTimerType = atoi(stimertype.c_str());

			time_t now = mytime(NULL);
			struct tm tm1;
			localtime_r(&now, &tm1);
			int Year = tm1.tm_year + 1900;
			int Month = tm1.tm_mon + 1;
			int Day = tm1.tm_mday;

			if (iTimerType == TTYPE_FIXEDDATETIME)
			{
				if (sdate.size() == 10)
				{
					Year = atoi(sdate.substr(0, 4).c_str());
					Month = atoi(sdate.substr(5, 2).c_str());
					Day = atoi(sdate.substr(8, 2).c_str());
				}
			}
			else if (iTimerType == TTYPE_MONTHLY)
			{
				smday = request::findValue(&req, "mday");
				if (smday == "") return;
			}
			else if (iTimerType == TTYPE_MONTHLY_WD)
			{
				soccurence = request::findValue(&req, "occurence");
				if (soccurence == "") return;
			}
			else if (iTimerType == TTYPE_YEARLY)
			{
				smday = request::findValue(&req, "mday");
				smonth = request::findValue(&req, "month");
				if ((smday == "") || (smonth == "")) return;
			}
			else if (iTimerType == TTYPE_YEARLY_WD)
			{
				smonth = request::findValue(&req, "month");
				soccurence = request::findValue(&req, "occurence");
				if ((smonth == "") || (soccurence == "")) return;
			}

			unsigned char hour = atoi(shour.c_str());
			unsigned char min = atoi(smin.c_str());
			unsigned char icmd = atoi(scmd.c_str());
			int days = atoi(sdays.c_str());
			unsigned char level = atoi(slevel.c_str());
			int mday = atoi(smday.c_str());
			int month = atoi(smonth.c_str());
			int occurence = atoi(soccurence.c_str());
			root["status"] = "OK";
			root["title"] = "AddSceneTimer";
			m_sql.safe_query(
				"INSERT INTO SceneTimers (Active, SceneRowID, [Date], Time, Type, UseRandomness, Cmd, Level, Days, MDay, Month, Occurence, TimerPlan) VALUES (%d,'%q','%04d-%02d-%02d','%02d:%02d',%d,%d,%d,%d,%d,%d,%d,%d,%d)",
				(active == "true") ? 1 : 0,
				idx.c_str(),
				Year, Month, Day,
				hour, min,
				iTimerType,
				(randomness == "true") ? 1 : 0,
				icmd,
				level,
				days,
				mday,
				month,
				occurence,
				m_sql.m_ActiveTimerPlan
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_UpdateSceneTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			std::string active = request::findValue(&req, "active");
			std::string stimertype = request::findValue(&req, "timertype");
			std::string sdate = request::findValue(&req, "date");
			std::string shour = request::findValue(&req, "hour");
			std::string smin = request::findValue(&req, "min");
			std::string randomness = request::findValue(&req, "randomness");
			std::string scmd = request::findValue(&req, "command");
			std::string sdays = request::findValue(&req, "days");
			std::string slevel = request::findValue(&req, "level");	//in percentage
			std::string smday = "0";
			std::string smonth = "0";
			std::string soccurence = "0";
			if (
				(idx == "") ||
				(active == "") ||
				(stimertype == "") ||
				(shour == "") ||
				(smin == "") ||
				(randomness == "") ||
				(scmd == "") ||
				(sdays == "")
				)
				return;

			unsigned char iTimerType = atoi(stimertype.c_str());

			time_t now = mytime(NULL);
			struct tm tm1;
			localtime_r(&now, &tm1);
			int Year = tm1.tm_year + 1900;
			int Month = tm1.tm_mon + 1;
			int Day = tm1.tm_mday;

			if (iTimerType == TTYPE_FIXEDDATETIME)
			{
				if (sdate.size() == 10)
				{
					Year = atoi(sdate.substr(0, 4).c_str());
					Month = atoi(sdate.substr(5, 2).c_str());
					Day = atoi(sdate.substr(8, 2).c_str());
				}
			}
			else if (iTimerType == TTYPE_MONTHLY)
			{
				smday = request::findValue(&req, "mday");
				if (smday == "") return;
			}
			else if (iTimerType == TTYPE_MONTHLY_WD)
			{
				soccurence = request::findValue(&req, "occurence");
				if (soccurence == "") return;
			}
			else if (iTimerType == TTYPE_YEARLY)
			{
				smday = request::findValue(&req, "mday");
				smonth = request::findValue(&req, "month");
				if ((smday == "") || (smonth == "")) return;
			}
			else if (iTimerType == TTYPE_YEARLY_WD)
			{
				smonth = request::findValue(&req, "month");
				soccurence = request::findValue(&req, "occurence");
				if ((smonth == "") || (soccurence == "")) return;
			}

			unsigned char hour = atoi(shour.c_str());
			unsigned char min = atoi(smin.c_str());
			unsigned char icmd = atoi(scmd.c_str());
			int days = atoi(sdays.c_str());
			unsigned char level = atoi(slevel.c_str());
			int mday = atoi(smday.c_str());
			int month = atoi(smonth.c_str());
			int occurence = atoi(soccurence.c_str());
			root["status"] = "OK";
			root["title"] = "UpdateSceneTimer";
			m_sql.safe_query(
				"UPDATE SceneTimers SET Active=%d, [Date]='%04d-%02d-%02d', Time='%02d:%02d', Type=%d, UseRandomness=%d, Cmd=%d, Level=%d, Days=%d, MDay=%d, Month=%d, Occurence=%d WHERE (ID == '%q')",
				(active == "true") ? 1 : 0,
				Year, Month, Day,
				hour, min,
				iTimerType,
				(randomness == "true") ? 1 : 0,
				icmd,
				level,
				days,
				mday,
				month,
				occurence,
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_DeleteSceneTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx == "")
				return;
			root["status"] = "OK";
			root["title"] = "DeleteSceneTimer";
			m_sql.safe_query(
				"DELETE FROM SceneTimers WHERE (ID == '%q')",
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_EnableSceneTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx == "")
				return;
			root["status"] = "OK";
			root["title"] = "EnableSceneTimer";
			m_sql.safe_query(
				"UPDATE SceneTimers SET Active=1 WHERE (ID == '%q')",
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_DisableSceneTimer(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx == "")
				return;
			root["status"] = "OK";
			root["title"] = "DisableSceneTimer";
			m_sql.safe_query(
				"UPDATE SceneTimers SET Active=0 WHERE (ID == '%q')",
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_ClearSceneTimers(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx == "")
				return;
			root["status"] = "OK";
			root["title"] = "ClearSceneTimer";
			m_sql.safe_query(
				"DELETE FROM SceneTimers WHERE (SceneRowID == '%q')",
				idx.c_str()
				);
			m_mainworker.m_scheduler.ReloadSchedules();
		}

		void CWebServer::Cmd_GetTimerPlans(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}
			root["status"] = "OK";
			root["title"] = "GetTimerPlans";
			std::vector<std::vector<std::string> > result;
			result = m_sql.safe_query("SELECT ID, Name FROM TimerPlans ORDER BY Name COLLATE NOCASE ASC");
			if (!result.empty())
			{
				int ii = 0;
				for (const auto & itt : result)
				{
					std::vector<std::string> sd = itt;
					root["result"][ii]["idx"] = sd[0];
					root["result"][ii]["Name"] = sd[1];
					ii++;
				}
			}
		}

		void CWebServer::Cmd_AddTimerPlan(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string name = HTMLSanitizer::Sanitize(request::findValue(&req, "name"));
			if (name.empty())
			{
				session.reply_status = reply::bad_request;
				return;
			}
			root["status"] = "OK";
			root["title"] = "AddTimerPlan";
			m_sql.safe_query("INSERT INTO TimerPlans (Name) VALUES ('%q')", name.c_str());
		}

		void CWebServer::Cmd_UpdateTimerPlan(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			std::string name = HTMLSanitizer::Sanitize(request::findValue(&req, "name"));
			if (name.empty())
			{
				session.reply_status = reply::bad_request;
				return;
			}

			root["status"] = "OK";
			root["title"] = "UpdateTimerPlan";

			m_sql.safe_query("UPDATE TimerPlans SET Name='%q' WHERE (ID == '%q')", name.c_str(), idx.c_str());
		}

		void CWebServer::Cmd_DeleteTimerPlan(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			int iPlan = atoi(idx.c_str());
			if (iPlan < 1)
				return;

			root["status"] = "OK";
			root["title"] = "DeletePlan";

			m_sql.safe_query("DELETE FROM Timers WHERE (TimerPlan == '%q')", idx.c_str());
			m_sql.safe_query("DELETE FROM SceneTimers WHERE (TimerPlan == '%q')", idx.c_str());
			m_sql.safe_query("DELETE FROM SetpointTimers WHERE (TimerPlan == '%q')", idx.c_str());

			m_sql.safe_query("DELETE FROM TimerPlans WHERE (ID == '%q')", idx.c_str());

			if (m_sql.m_ActiveTimerPlan == iPlan)
			{
				//Set active timer plan to default
				m_sql.UpdatePreferencesVar("ActiveTimerPlan", 0);
				m_sql.m_ActiveTimerPlan = 0;
				m_mainworker.m_scheduler.ReloadSchedules();
			}
		}

		void CWebServer::Cmd_DuplicateTimerPlan(WebEmSession & session, const request& req, Json::Value &root)
		{
			if (session.rights != URIGHTS_ADMIN)
			{
				session.reply_status = reply::forbidden;
				return; //Only admin user allowed
			}

			std::string idx = request::findValue(&req, "idx");
			if (idx.empty())
				return;
			std::string name = HTMLSanitizer::Sanitize(request::findValue(&req, "name"));
			if (
				(name.empty())
				)
				return;

			root["status"] = "OK";
			root["title"] = "DuplicateTimerPlan";

			m_sql.safe_query("INSERT INTO TimerPlans (Name) VALUES ('%q')", name.c_str());

			std::vector<std::vector<std::string> > result;

			result = m_sql.safe_query("SELECT MAX(ID) FROM TimerPlans WHERE (Name=='%q')", name.c_str());
			if (!result.empty())
			{
				std::string nID = result[0][0];

				//Normal Timers
				result = m_sql.safe_query("SELECT Active, DeviceRowID, Time, Type, Cmd, Level, Days, UseRandomness, Hue, [Date], MDay, Month, Occurence, Color FROM Timers WHERE (TimerPlan==%q) ORDER BY ID", idx.c_str());
				for (const auto & itt : result)
				{
					std::vector<std::string> sd = itt;
					m_sql.safe_query(
						"INSERT INTO Timers (Active, DeviceRowID, Time, Type, Cmd, Level, Days, UseRandomness, Hue, [Date], MDay, Month, Occurence, Color, TimerPlan) VALUES (%q, %q, '%q', %q, %q, %q, %q, %q, %q, '%q', %q, %q, %q, '%q', %q)",
						sd[0].c_str(),
						sd[1].c_str(),
						sd[2].c_str(),
						sd[3].c_str(),
						sd[4].c_str(),
						sd[5].c_str(),
						sd[6].c_str(),
						sd[7].c_str(),
						sd[8].c_str(),
						sd[9].c_str(),
						sd[10].c_str(),
						sd[11].c_str(),
						sd[12].c_str(),
						sd[13].c_str(),
						nID.c_str()
					);
				}
				//Scene Timers
				result = m_sql.safe_query("SELECT Active, SceneRowID, Time, Type, Cmd, Level, Days, UseRandomness, Hue, [Date], Month, MDay, Occurence FROM SceneTimers WHERE (TimerPlan==%q) ORDER BY ID", idx.c_str());
				for (const auto & itt : result)
				{
					std::vector<std::string> sd = itt;
					m_sql.safe_query(
						"INSERT INTO SceneTimers (Active, SceneRowID, Time, Type, Cmd, Level, Days, UseRandomness, Hue, [Date], Month, MDay, Occurence, TimerPlan) VALUES (%q, %q, '%q', %q, %q, %q, %q, %q, %q, '%q', %q, %q, %q, %q)",
						sd[0].c_str(),
						sd[1].c_str(),
						sd[2].c_str(),
						sd[3].c_str(),
						sd[4].c_str(),
						sd[5].c_str(),
						sd[6].c_str(),
						sd[7].c_str(),
						sd[8].c_str(),
						sd[9].c_str(),
						sd[10].c_str(),
						sd[11].c_str(),
						sd[12].c_str(),
						nID.c_str()
					);
				}
				//Setpoint Timers
				result = m_sql.safe_query("SELECT Active, DeviceRowID, [Date], Time, Type, Temperature, Days, Month, MDay, Occurence FROM SetpointTimers WHERE (TimerPlan==%q) ORDER BY ID", idx.c_str());
				for (const auto & itt : result)
				{
					std::vector<std::string> sd = itt;
					m_sql.safe_query(
						"INSERT INTO SetpointTimers (Active, DeviceRowID, [Date], Time, Type, Temperature, Days, Month, MDay, Occurence, TimerPlan) VALUES (%q, %q, '%q', '%q', %q, %q, %q, %q, %q, %q, %q)",
						sd[0].c_str(),
						sd[1].c_str(),
						sd[2].c_str(),
						sd[3].c_str(),
						sd[4].c_str(),
						sd[5].c_str(),
						sd[6].c_str(),
						sd[7].c_str(),
						sd[8].c_str(),
						sd[9].c_str(),
						nID.c_str()
					);
				}
			}
		}

	}
}
