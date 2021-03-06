#pragma once

#include <Windows.h>
#include <Psapi.h>
#include <map>

#include "peconv.h"
#include "module_scan_report.h"
#include "mempage_scanner.h"

#define INVALID_OFFSET (-1)
#define PE_NOT_FOUND 0

bool is_valid_section(BYTE *loadedData, size_t loadedSize, BYTE *hdr_ptr, DWORD charact);

class PeArtefacts {
public:
	static const size_t JSON_LEVEL = 1;

	PeArtefacts() {
		regionStart = INVALID_OFFSET;
		peBaseOffset = INVALID_OFFSET;
		ntFileHdrsOffset = INVALID_OFFSET;
		secHdrsOffset = INVALID_OFFSET;
		secCount = 0;
		calculatedImgSize = 0;
		isMzPeFound = false;
		isDll = true;
	}

	bool hasNtHdrs()
	{
		return (ntFileHdrsOffset != INVALID_OFFSET);
	}

	bool hasSectionHdrs()
	{
		return (secHdrsOffset != INVALID_OFFSET);
	}
	
	ULONGLONG peImageBase()
	{
		return this->peBaseOffset + this->regionStart;
	}

	const virtual bool fieldsToJSON(std::stringstream &outs, size_t level = JSON_LEVEL)
	{
		OUT_PADDED(outs, level, "\"pe_base_offset\" : ");
		outs << "\"" << std::hex << peBaseOffset << "\"";
		if (hasNtHdrs()) {
			outs << ",\n";
			OUT_PADDED(outs, level, "\"nt_file_hdr\" : ");
			outs << "\"" << std::hex << ntFileHdrsOffset << "\"";
		}
		outs << ",\n";
		OUT_PADDED(outs, level, "\"sections_hdrs\" : ");
		outs << "\"" << std::hex << secHdrsOffset << "\"";
		outs << ",\n";
		OUT_PADDED(outs, level, "\"sections_count\" : ");
		outs << std::hex << secCount;
		outs << ",\n";
		OUT_PADDED(outs, level, "\"is_dll\" : ");
		outs << std::dec << isDll;
		return true;
	}
	
	const virtual bool toJSON(std::stringstream &outs, size_t level = JSON_LEVEL)
	{
		OUT_PADDED(outs, level, "\"pe_artefacts\" : {\n");
		fieldsToJSON(outs, level + 1);
		outs << "\n";
		OUT_PADDED(outs, level, "}");
		return true;
	}

	LONGLONG regionStart;
	size_t peBaseOffset; //offset from the regionStart (PE may not start at the first page of the region)
	size_t ntFileHdrsOffset; //offset from the regionStart
	size_t secHdrsOffset; //offset from the regionStart
	size_t secCount;
	size_t calculatedImgSize;
	bool isMzPeFound;
	bool isDll;
};

class ArtefactScanReport : public MemPageScanReport
{
public:
	ArtefactScanReport(HANDLE processHandle, HMODULE _module, size_t _moduleSize, t_scan_status status, PeArtefacts &peArt)
		: MemPageScanReport(processHandle, _module, _moduleSize, status),
		artefacts(peArt), 
		initialRegionSize(_moduleSize)
	{
		is_executable = true;
		protection = 0;
		has_pe = true;
		has_shellcode = false;

		size_t total_region_size = peArt.calculatedImgSize + peArt.peBaseOffset;
		if (total_region_size > this->moduleSize) {
			this->moduleSize = total_region_size;
		}
	}

	const virtual void fieldsToJSON(std::stringstream &outs, size_t level = JSON_LEVEL)
	{
		MemPageScanReport::fieldsToJSON(outs, level);
		outs << ",\n";
		artefacts.toJSON(outs, level);
	}

	const virtual bool toJSON(std::stringstream &outs, size_t level = JSON_LEVEL)
	{
		OUT_PADDED(outs, level, "\"workingset_scan\" : {\n");
		fieldsToJSON(outs, level + 1);
		outs << "\n";
		OUT_PADDED(outs, level, "}");
		return true;
	}

	PeArtefacts artefacts;
	size_t initialRegionSize;
};

class ArtefactScanner {
public:
	ArtefactScanner(HANDLE _procHndl, MemPageData &_memPageData)
		: processHandle(_procHndl), 
		memPage(_memPageData), prevMemPage(nullptr), artPagePtr(nullptr)
	{
	}

	virtual ~ArtefactScanner()
	{
		deletePrevPage();
	}

	virtual ArtefactScanReport* scanRemote();

protected:
	class ArtefactsMapping
	{
	public:
		ArtefactsMapping(MemPageData &_memPage) :
			memPage(_memPage)
		{
			pe_image_base = PE_NOT_FOUND;
			nt_file_hdr = nullptr;
			sec_hdr = nullptr;
			isMzPeFound = false;
		}

		bool foundAny()
		{
			if (sec_hdr || nt_file_hdr) {
				return true;
			}
			return false;
		}

		MemPageData &memPage;
		ULONGLONG pe_image_base;
		IMAGE_FILE_HEADER* nt_file_hdr;
		IMAGE_SECTION_HEADER* sec_hdr;
		bool isMzPeFound;
	};

	void deletePrevPage()
	{
		delete this->prevMemPage;
		this->prevMemPage = nullptr;
		this->artPagePtr = nullptr;
	}

	bool hasShellcode(HMODULE region_start, size_t region_size, PeArtefacts &peArt);

	bool findMzPe(ArtefactsMapping &mapping);
	bool setMzPe(ArtefactsMapping &mapping, IMAGE_DOS_HEADER* _dos_hdr);
	bool setSecHdr(ArtefactsMapping &mapping, IMAGE_SECTION_HEADER* _sec_hdr);
	bool setNtFileHdr(ArtefactScanner::ArtefactsMapping &aMap, IMAGE_FILE_HEADER* _nt_hdr);
	PeArtefacts *generateArtefacts(ArtefactsMapping &aMap);

	PeArtefacts* findArtefacts(MemPageData &memPage);
	PeArtefacts* findInPrevPages(ULONGLONG addr_start, ULONGLONG addr_stop);

	ULONGLONG calcPeBase(MemPageData &memPage, BYTE *hdr_ptr);
	size_t calcImageSize(MemPageData &memPage, IMAGE_SECTION_HEADER *hdr_ptr, ULONGLONG pe_image_base);

	IMAGE_FILE_HEADER* findNtFileHdr(BYTE* loadedData, size_t loadedSize);
	BYTE* findSecByPatterns(MemPageData &memPage);
	IMAGE_SECTION_HEADER* findSectionsHdr(MemPageData &memPageData);
	IMAGE_DOS_HEADER* findMzPeHeader(MemPageData &memPage);

	HANDLE processHandle;
	MemPageData &memPage;
	MemPageData *prevMemPage;
	MemPageData *artPagePtr; //pointer to the page where the artefacts were found: either to memPage or to prevMemPage
};
