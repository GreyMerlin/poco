//
// DNS.cpp
//
// $Id: //poco/1.4/Net/src/DNS.cpp#10 $
//
// Library: Net
// Package: NetCore
// Module:  DNS
//
// Copyright (c) 2005-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/Net/DNS.h"
#include "Poco/Net/NetException.h"
#include "Poco/Net/SocketAddress.h"
#include "Poco/Environment.h"
#include "Poco/NumberFormatter.h"
#include "Poco/RWLock.h"
#include <cstring>


#if defined(POCO_HAVE_LIBRESOLV)
#include <resolv.h>
#endif


using Poco::Environment;
using Poco::NumberFormatter;
using Poco::IOException;


namespace Poco {
namespace Net {


#if defined(POCO_HAVE_LIBRESOLV)
static Poco::RWLock resolverLock;
#endif


HostEntry DNS::hostByName(const std::string& hostname)
{
#if defined(POCO_HAVE_LIBRESOLV)
	Poco::ScopedReadRWLock readLock(resolverLock);
#endif
	
#if defined(POCO_HAVE_IPv6) || defined(POCO_HAVE_ADDRINFO)
	struct addrinfo* pAI;
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
	int rc = getaddrinfo(hostname.c_str(), NULL, &hints, &pAI); 
	if (rc == 0)
	{
		HostEntry result(pAI);
		freeaddrinfo(pAI);
		return result;
	}
	else
	{
		aierror(rc, hostname);
	}
#elif defined(POCO_VXWORKS)
	int addr = hostGetByName(const_cast<char*>(hostname.c_str()));
	if (addr != ERROR)
	{
		return HostEntry(hostname, IPAddress(&addr, sizeof(addr)));
	}
#else
	struct hostent* he = gethostbyname(hostname.c_str());
	if (he)
	{
		return HostEntry(he);
	}
#endif
	error(lastError(), hostname); // will throw an appropriate exception
	throw NetException(); // to silence compiler
}


HostEntry DNS::hostByAddress(const IPAddress& address)
{
#if defined(POCO_HAVE_LIBRESOLV)
	Poco::ScopedReadRWLock readLock(resolverLock);
#endif

#if defined(POCO_HAVE_IPv6) || defined(POCO_HAVE_ADDRINFO)
	SocketAddress sa(address, 0);
	static char fqname[1024];
	int rc = getnameinfo(sa.addr(), sa.length(), fqname, sizeof(fqname), NULL, 0, NI_NAMEREQD); 
	if (rc == 0)
	{
		struct addrinfo* pAI;
		struct addrinfo hints;
		std::memset(&hints, 0, sizeof(hints));
		hints.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
		rc = getaddrinfo(fqname, NULL, &hints, &pAI);
		if (rc == 0)
		{
			HostEntry result(pAI);
			freeaddrinfo(pAI);
			return result;
		}
		else
		{
			aierror(rc, address.toString());
		}
	}
	else
	{
		aierror(rc, address.toString());
	}
#elif defined(POCO_VXWORKS)
	char name[MAXHOSTNAMELEN + 1];
	if (hostGetByAddr(*reinterpret_cast<const int*>(address.addr()), name) == OK)
	{
		return HostEntry(std::string(name), address);
	}
#else
	struct hostent* he = gethostbyaddr(reinterpret_cast<const char*>(address.addr()), address.length(), address.af());
	if (he)
	{
		return HostEntry(he);
	}
#endif
	int err = lastError();
	error(err, address.toString());      // will throw an appropriate exception
	throw NetException(); // to silence compiler
}


HostEntry DNS::resolve(const std::string& address)
{
	IPAddress ip;
	if (IPAddress::tryParse(address, ip))
		return hostByAddress(ip);
	else
		return hostByName(address);
}


IPAddress DNS::resolveOne(const std::string& address)
{
	const HostEntry& entry = resolve(address);
	if (!entry.addresses().empty())
		return entry.addresses()[0];
	else
		throw NoAddressFoundException(address);
}


HostEntry DNS::thisHost()
{
	return hostByName(hostName());
}


void DNS::reload()
{
#if defined(POCO_HAVE_LIBRESOLV)
	Poco::ScopedWriteRWLock writeLock(resolverLock);
	res_init();
#endif
}


void DNS::flushCache()
{
}


std::string DNS::hostName()
{
	char buffer[256];
	int rc = gethostname(buffer, sizeof(buffer));
	if (rc == 0)
		return std::string(buffer);
	else
		throw NetException("Cannot get host name");
}


int DNS::lastError()
{
#if defined(_WIN32)
	return GetLastError();
#elif defined(POCO_VXWORKS)
	return errno;
#else
	return h_errno;
#endif
}

	
void DNS::error(int code, const std::string& arg)
{
	switch (code)
	{
	case POCO_ESYSNOTREADY:
		throw NetException("Net subsystem not ready");
	case POCO_ENOTINIT:
		throw NetException("Net subsystem not initialized");
	case POCO_HOST_NOT_FOUND:
		throw HostNotFoundException(arg);
	case POCO_TRY_AGAIN:
		throw DNSException("Temporary DNS error while resolving", arg);
	case POCO_NO_RECOVERY:
		throw DNSException("Non recoverable DNS error while resolving", arg);
	case POCO_NO_DATA:
		throw NoAddressFoundException(arg);
	default:
		throw IOException(NumberFormatter::format(code));
	}
}


void DNS::aierror(int code, const std::string& arg)
{
#if defined(POCO_HAVE_IPv6) || defined(POCO_HAVE_ADDRINFO)
	switch (code)
	{
	case EAI_AGAIN:
		throw DNSException("Temporary DNS error while resolving", arg);
	case EAI_FAIL:
		throw DNSException("Non recoverable DNS error while resolving", arg);
#if !defined(_WIN32) // EAI_NODATA and EAI_NONAME have the same value
#if defined(EAI_NODATA) // deprecated in favor of EAI_NONAME on FreeBSD
	case EAI_NODATA:
		throw NoAddressFoundException(arg);
#endif
#endif
	case EAI_NONAME:
		throw HostNotFoundException(arg);
#if defined(EAI_SYSTEM)
	case EAI_SYSTEM:
		error(lastError(), arg);
		break;
#endif
#if defined(_WIN32)
	case WSANO_DATA: // may happen on XP
		throw HostNotFoundException(arg);
#endif
	default:
		throw DNSException("EAI", NumberFormatter::format(code));
	}
#endif // POCO_HAVE_IPv6 || defined(POCO_HAVE_ADDRINFO)
}


void initializeNetwork()
{
#if defined(_WIN32)
	WORD    version = MAKEWORD(2, 2);
	WSADATA data;
	if (WSAStartup(version, &data) != 0)
		throw NetException("Failed to initialize network subsystem");
#endif // _WIN32
}
		

void uninitializeNetwork()
{
#if defined(_WIN32)
	WSACleanup();
#endif // _WIN32
}


} } // namespace Poco::Net
