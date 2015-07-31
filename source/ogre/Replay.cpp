#include "pch.h"
#include "Replay.h"
#include "common/Def_Str.h"
#include <OgreTimer.h>
#include <string>
#include "CHud.h" //
using namespace std;


/*TODO:
	 endianness
	 conv tool for all
	~check back save+load
	`load old rpl as new
	`play new rpl game
	 car frame as new rpl
	 save new rpls
	 only new rpl use
/**/
			
//  header
//----------------------------------------------------------------
ReplayHeader2::ReplayHeader2()
{
	Default();
}
void ReplayHeader2::Default()
{
	head[0] = 'S';  head[1] = 'R';  head[2] = '/';  head[3] = '^';  head[4] = 0;
	track = "";  track_user = 0;
	ver = 21;  time = 0.f;
	
	numPlayers = 0;  trees = 1.f;
	num_laps = 1;  networked = 0;
	sim_mode = "";

	cars.clear();  numWh.clear();  nicks.clear();
}

void ReplayHeader2::FromOld(const struct ReplayHeader& h)
{
	time = 0.f;  //set later
	track = h.track;
	track_user = h.track_user;
	
	numPlayers = h.numPlayers;
	cars.clear();  cars.resize(numPlayers);
	numWh.clear();  numWh.resize(numPlayers);

	cars[0] = h.car;
	int i;
	for (i=1; i < numPlayers; ++i)
		cars[i] = h.cars[i-1];
	
	for (i=0; i < numPlayers; ++i)
	{	string s = cars[i];  char w = 4;
		if (s=="BV")  w = 2;  else  // old, not 4 wheeled veh
		if (s=="O"||s=="V1"||s=="V2"||s=="V3")  w = 0;
		numWh[i] = w;
	}
	trees = h.trees;
	num_laps = h.num_laps;
	networked = h.networked;
	sim_mode = h.sim_mode;

	nicks.clear();
	if (networked)
	{	nicks.resize(numPlayers);
		for (i=0; i < numPlayers; ++i)
			nicks[i] = h.nicks[i];
	}
}

ReplayFrame2::ReplayFrame2()
	:gear(0),fl(0)  //..
{
}

///  convert old frame to new
//-------------------------------------------------------------------------------
void ReplayFrame2::FromOld(const struct ReplayFrame& f, uchar numWh)
{
	time = f.time;  // save once..
	pos = f.pos;  rot = f.rot;
	
	fl = 0;  // zero flags
	set(b_braking, f.braking);

	//  hud
	gear = f.gear;
	rpm = f.rpm;  vel = f.vel;
	percent = f.percent / 255.f;  // track %
	damage = 0.f;  // wasnt saved

	//  sound, input
	throttle = f.throttle / 255.f;	steer = f.steer / 127.f;
	fboost = f.fboost / 255.f;		clutch = f.clutch / 255.f;
	speed = f.speed;  dynVel = f.dynVel;
	hov_roll = f.hov_roll;  //=sph_yaw for O


	//  hit continuous  ---
	bool hasScr = f.fCarScrap > 0.f || f.fCarScreech > 0.f;
	set(b_scrap, hasScr);
	if (hasScr)
	{	RScrap scr;
		scr.fScrap = half(f.fCarScrap);  scr.fScreech = half(f.fCarScreech);
		scrap.clear();  scrap.push_back(scr);
	};
	
	//  new hit data impact
	fHitTime = f.fHitTime;
	bool h = fHitTime == 1.f;  // wrong if saving every nth frame..
	set(b_hit, h);
	if (h)
	{	RHit ht;
		ht.fParIntens = f.fParIntens;  ht.fParVel = f.fParVel;
		ht.vHitPos = f.vHitPos;  ht.vHitNorm = f.vHitNorm;  // world
		ht.fHitForce = f.fHitForce;
		hit.push_back(ht);
	};

	//  wheels  ---
	wheels.clear();
	for (int i=0; i < numWh; ++i)
	{	RWheel wh;
	
		wh.pos = f.whPos[i];
		for (int q=0; q<4; ++q)  wh.rot[q] = f.whRot[i][q];

		//  wheel trails, particles, snd
		wh.surfType = f.surfType[i];    wh.whTerMtr = f.whTerMtr[i];
		wh.whRoadMtr = f.whRoadMtr[i];  wh.whP = f.whP[i];  //particle type

		wh.squeal = f.squeal[i];  wh.slide = f.slide[i];  wh.whVel = f.whVel[i];
		wh.suspVel = f.suspVel[i];  wh.suspDisp = f.suspDisp[i];

		////  fluids
		wh.whH = f.whH[i] / 255.f;  // submerge
		wh.whAngVel = f.whAngVel[i];
		wh.whSteerAng = i >= 2 ? 0.f : f.whSteerAng[i];
		
		wheels.push_back(wh);
	};
}


Replay2::Replay2()
	:idLast(0)
{
	Clear();
}

//  Init  once per game
void Replay2::InitHeader(const char* track, bool trk_user, bool bClear)
{
	header.Default();
	header.track = track;  header.track_user = trk_user ? 1 : 0;
	if (bClear)
		Clear();
}
void Replay2::Clear(bool time)
{
	idLast = 0;
	if (time)
		header.time = 0.f;
	
	frames.resize(header.numPlayers);
	for (int p=0; p < header.numPlayers; ++p)
	{	frames[p].clear();
		frames[p].reserve(cDefSize);
	}
}


///  Load
//-------------------------------------------------------------------------------------------------------
bool Replay2::LoadFile(string file, bool onlyHdr)
{
	ifstream fi(file.c_str(), ios::binary | ios::in);
	if (!fi)  return false;

	Ogre::Timer ti;
	
	//  header check
	fi.read(header.head,5);
	if (strcmp(header.head,"SR\\_")==0)
	{	LogO(">- Load replay2 --  file: "+file+"  Error, loading old!");
		// todo: load old, convert / use old
		return false;
	}
	else if (strcmp(header.head,"SR/^")!=0)
	{	LogO(">- Load replay2 --  file: "+file+"  Error: Unknown header");
		return false;
	}
	
	uchar l;  int i,s;  char buf[256];
	#define rd(a)  fi.read((char*)&a, sizeof(a))
	#define rs(s)  {  fi.read((char*)&l, 1);  fi.read(buf, l);  buf[l]=0;  s = buf;  }  //string

	//  header  ------
	ReplayHeader2& h = header;
	rd(h.ver);  rd(h.time);
	rs(h.track)  rd(h.track_user);
	Ogre::String ss = ">-   ";
	ss += h.track+"  ";
	ss += CHud::StrTime(h.time)+"  ";

	rd(h.numPlayers);  s = h.numPlayers;
	h.cars.clear();  h.cars.resize(s);
	h.numWh.clear();  h.numWh.resize(s);
	for (i=0; i < s; ++i)
	{	rs(h.cars[i])  ss += h.cars[i]+" ";  }  ss+=" ";
	for (i=0; i < s; ++i)
	{	rd(h.numWh[i]);  ss += toStr(h.numWh[i])+" ";  }  ss+=" ";

	rd(h.trees);  rd(h.num_laps);
	rd(h.networked);  rs(h.sim_mode)

	h.nicks.clear();  h.nicks.resize(s);
	if (h.networked)
		for (i=0; i < s; ++i)
		{	rs(h.nicks[i])  ss += h.nicks[i]+" ";  }  ss+=" ";

	#ifdef LOG_RPL
		LogO(ss);
		if (!onlyHdr)
			LogO(">- Load replay2 --  file: "+file+"  players:"+toStr(h.numPlayers));
	#endif
	
	//  clear
	Clear(false);
	if (onlyHdr)
	{
		fi.close();  return true;
	}
	
	//  frames  ------
	i=0;  int p,w;
	while (!fi.eof())
	{
		//rd(f.time);  // once..?
		for (p=0; p < header.numPlayers; ++p)
		{
			ReplayFrame2 f;
			rd(f.time);
			//  car
			rd(f.pos);  rd(f.rot);  rd(f.fl);  //b_braking etc
			//  hud
			rd(f.gear);  rd(f.rpm);  rd(f.vel);
			rd(f.damage);  rd(f.clutch);  rd(f.percent);
			//  sound, input
			rd(f.throttle);  rd(f.steer);  rd(f.fboost);
			rd(f.speed);  rd(f.dynVel);
			//  ext
			if (f.get(b_hov))  rd(f.hov_roll);
			bool flu = f.get(b_fluid);
			if (flu)  rd(f.whMudSpin);
			
			//  wheels
			int ww = header.numWh[p];
			for (w=0; w < ww; ++w)
			{
				ReplayFrame2::RWheel wh;
				rd(wh.pos);  rd(wh.rot);
				//  trl, par, snd
				rd(wh.surfType);  rd(wh.whTerMtr);
				rd(wh.whRoadMtr);  rd(wh.whP);
				//  tire
				rd(wh.squeal);  rd(wh.slide);  rd(wh.whVel);
				rd(wh.suspVel);  rd(wh.suspDisp);
				//  fluids
				if (flu)  rd(wh.whH);
				rd(wh.whAngVel);  rd(wh.whSteerAng);
				
				f.wheels.push_back(wh);
			}

			//  hit data
			if (f.get(b_scrap))
			{
				ReplayFrame2::RScrap s;
				rd(s.fScrap);  rd(s.fScreech);
				f.scrap.push_back(s);
			}
			rd(f.fHitTime);
			if (f.get(b_hit))
			{
				ReplayFrame2::RHit h;
				rd(h.fHitForce);  rd(h.fParIntens);  rd(h.fParVel);
				rd(h.vHitPos.x);   rd(h.vHitPos.y);   rd(h.vHitPos.z);
				rd(h.vHitNorm.x);  rd(h.vHitNorm.y);  rd(h.vHitNorm.z);
				f.hit.push_back(h);
			}
			

			if (i > 0 && f.time < frames[p][i-1].time)
			{
				#ifdef LOG_RPL
					LogO(">- Load replay2  BAD frame time  id:"+toStr(i)+"  plr:"+toStr(p)
						+"  t-1:"+fToStr(frames[p][i-1].time,5,7)+" > t:"+fToStr(f.time,5,7));
				#endif
			}else
			if (!fi.eof())
				frames[p].push_back(f);
		}
		++i;
	}

	fi.close();

    #ifdef LOG_RPL
		LogO(">- Load replay2  first: "+fToStr(frames[0][0].time,5,7)+
			"  time: "+fToStr(GetTimeLength(),2,5)+"  frames: "+toStr(frames[0].size()));
	#endif

	LogO(Ogre::String("::: Time Replay2 Load: ") + fToStr(ti.getMilliseconds(),0,3) + " ms");
	return true;
}

///  Save
//-------------------------------------------------------------------------------------------------------
bool Replay2::SaveFile(string file)
{
	//if (header.numPlayers < 1)  return false;
	
	ofstream of(file.c_str(), ios::binary | ios::out);
	if (!of)  return false;

	uchar l;  int i,s;
	#define ws(s)  {  l = s.length();  of.write((char*)&l, 1);  of.write(s.c_str(), l);  }  //string
	#define wr(a)  of.write((char*)&a, sizeof(a))
	//todo: portability, endianness for shorts?..

	//  header  ------
	const ReplayHeader2& h = header;
	of.write((char*)&h.head, 5);
	wr(h.ver);  wr(h.time);
	ws(h.track)  wr(h.track_user);

	wr(h.numPlayers);
	s = h.numPlayers;  // car names
	for (i=0; i < s; ++i)  ws(h.cars[i])
	for (i=0; i < s; ++i)  wr(h.numWh[i]);

	wr(h.trees);  wr(h.num_laps);
	wr(h.networked);  ws(h.sim_mode)

	if (h.networked)
		for (i=0; i < s; ++i)  ws(h.nicks[i])

	
	//  frames  ------
	s = frames[0].size();  int p,w;
	//s = 1;  p = 1;  //test

	for (i=0; i < s; ++i)
	for (p=0; p < header.numPlayers; ++p)
	{
		ReplayFrame2 f = frames[p][i];
		wr(f.time);
		//  car
		wr(f.pos);  wr(f.rot);  wr(f.fl);  //b_braking etc
		//  hud
		wr(f.gear);  wr(f.rpm);  wr(f.vel);
		wr(f.damage);  wr(f.clutch);  wr(f.percent);
		//  sound, input
		wr(f.throttle);  wr(f.steer);  wr(f.fboost);
		wr(f.speed);  wr(f.dynVel);
		//  ext
		if (f.get(b_hov))  wr(f.hov_roll);
		bool flu = f.get(b_fluid);
		if (flu)  wr(f.whMudSpin);
		
		//  wheels
		int ww = f.wheels.size();
		for (w=0; w < ww; ++w)
		{
			const ReplayFrame2::RWheel& wh = f.wheels[w];
			wr(wh.pos);  wr(wh.rot);
			//  trl, par, snd
			wr(wh.surfType);  wr(wh.whTerMtr);
			wr(wh.whRoadMtr);  wr(wh.whP);
			//  tire
			wr(wh.squeal);  wr(wh.slide);  wr(wh.whVel);
			wr(wh.suspVel);  wr(wh.suspDisp);
			//  fluids
			if (flu)  wr(wh.whH);
			wr(wh.whAngVel);  wr(wh.whSteerAng);
		}

		//  hit data
		if (f.get(b_scrap) /*&& scrap.size()==1*/)
		{
			const ReplayFrame2::RScrap& s = f.scrap[0];
			wr(s.fScrap);  wr(s.fScreech);
		}
		wr(f.fHitTime);
		if (f.get(b_hit) /*&& hit.size()==1*/)
		{
			const ReplayFrame2::RHit& h = f.hit[0];
			wr(h.fHitForce);  wr(h.fParIntens);  wr(h.fParVel);
			wr(h.vHitPos.x);   wr(h.vHitPos.y);   wr(h.vHitPos.z);
			wr(h.vHitNorm.x);  wr(h.vHitNorm.y);  wr(h.vHitNorm.z);
		}
	}

    of.close();
    return true;
}
//-------------------------------------------------------------------------------------------------------


//  add (Record)
void Replay2::AddFrame(const ReplayFrame2& frame, int carNum)
{
	if (carNum > 0 || frame.time > GetTimeLength())  // dont add before last
	{
		frames[carNum].push_back(frame);
		header.time = frame.time;
	}
}

//  CopyFrom
void Replay2::CopyFrom(const Replay2& rpl)
{
	Clear();
	header.time = rpl.header.time;

	//  plr 1 only, for ghost
	for (int i=0; i < rpl.GetNumFrames(); ++i)
		frames[0].push_back(rpl.frames[0][i]);
}

//  last frame time, sec
const float Replay2::GetTimeLength() const
{
	return header.time;
}


///  get (Play)
//----------------------------------------------------------------
bool Replay2::GetFrame(float time, ReplayFrame2* pFr, int carNum)
{
	int& ic = idLast;  // last index

	int s = frames[carNum].size();
	if (ic > s-1)  ic = s-1;  // new size
	if (s < 2)  return false;  // empty

	///  find which frame for given time
	//if (ic+2 == s)  return false;  // end

	//  search up
	while (ic+1 < s-1 && frames[carNum][ic+1].time <= time)  ++ic;
	//  search down
	while (ic > 0     && frames[carNum][ic].time > time)  --ic;

	if (ic < 0 || ic >= s)
		return false;  //-

	///  simple, no interpolation
	#if 1
	if (pFr)
		*pFr = frames[carNum][ic];
	#else
	//  linear interp ..
	if (pFr)
	{
		//  cur <= time < next
		const ReplayFrame& fc = frames[ic], fn = frames[ic+1];

		float m = (time - fc.time) / (fn.time - fc.time);  // [0..1]
		//Ogre::LogManager::getSingleton().logMessage(toStr(m));
		
		ReplayFrame fr = fc;
		fr.pos = (fn.pos - fc.pos) * m + fc.pos;

		for (int w=0; w<4; ++w)
		{
			fr.whPos[w] = (fn.whPos[w] - fc.whPos[w]) * m + fc.whPos[w];
		//Quaternion q;  q.Slerp(m, fc.rot, fn.rot);
		}
		//... all data
		*pFr = fr;
	}
	#endif

	//  last time
	float end = header.time;  //frames[carNum][s-1].time;
	if (time >= end)
	{
		pFr->fboost = 0.f;
		//  clear emitters at end
		for (int w=0; w < 4; ++w)
		{
			//pFr->slide[w] = 0.f;
			//pFr->squeal[w] = 0.f;
			//pFr->whVel[w] = 0.f;
		}
	}
	
	//  check if ended
	return time <= end;
}

//  delete frames after current time (when time did go back)
void Replay2::DeleteFrames(int c, float fromTime)
{
	if (frames[c].empty())  return;
	while (!frames[c].empty() && frames[c][ frames[c].size()-1].time >= fromTime)
		frames[c].pop_back();
}

