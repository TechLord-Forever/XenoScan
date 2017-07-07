#include "ScanVariant.h"
#include "Scanner.h"

/*
	This cannot be in ScanVariantTypeTraits.h because it needs to know the details
	of ScanVariant. This cannot be in it's own .cpp file because templated member function
	declarations are only visible in the .cpp file in which they are declared.
*/
template <typename TYPE, bool UNSIGNED, bool FLOATING>
void ScanVariantUnderlyingNumericTypeTraits<TYPE, UNSIGNED, FLOATING>::fromString(const std::wstring& input, ScanVariant& output) const
{
	TYPE value;
	uint8_t buffer[sizeof(int64_t)];
	if (swscanf_s(input.c_str(), this->typeFormat.c_str(), &buffer[0]) == -1)
	{
		output = ScanVariant();
		return;
	}
	memcpy(&value, &buffer[0], sizeof(value));
	output = ScanVariant();
}

ScanVariantUnderlyingTypeTraits* ScanVariant::UnderlyingTypeTraits[ScanVariant::SCAN_VARIANT_NULL + 1] =
{
	new ScanVariantUnderlyingAsciiStringTypeTraits(),
	new ScanVariantUnderlyingWideStringTypeTraits(),

	new ScanVariantUnderlyingNumericTypeTraits<uint8_t, true, false>(L"uint8", L"%u"),
	new ScanVariantUnderlyingNumericTypeTraits<int8_t, false, false>(L"int8", L"%d"),

	new ScanVariantUnderlyingNumericTypeTraits<uint16_t, true, false>(L"uint16", L"%u"),
	new ScanVariantUnderlyingNumericTypeTraits<int16_t, false, false>(L"int16", L"%d"),

	new ScanVariantUnderlyingNumericTypeTraits<uint32_t, true, false>(L"uint32", L"%u"),
	new ScanVariantUnderlyingNumericTypeTraits<int32_t, false, false>(L"int32", L"%d"),

	new ScanVariantUnderlyingNumericTypeTraits<uint64_t, true, false>(L"uint64", L"%llu"),
	new ScanVariantUnderlyingNumericTypeTraits<int64_t, false, false>(L"int64", L"%lld"),

	new ScanVariantUnderlyingNumericTypeTraits<double, true, true>(L"double", L"%f"),
	new ScanVariantUnderlyingNumericTypeTraits<float, true, true>(L"float", L"%f"),

	new ScanVariantUnderlyingStructureTypeTraits(),

	new ScanVariantUnderlyingNullTypeTraits()
};

ScanVariant ScanVariant::MakePlaceholder(ScanVariantType type)
{
	ASSERT(type >= SCAN_VARIANT_NUMERICTYPES_BEGIN && type <= SCAN_VARIANT_NUMERICTYPES_END);

	auto offset = type - SCAN_VARIANT_NUMERICTYPES_BEGIN;

	ScanVariant temp;
	temp.type = SCAN_VARIANT_PLACEHOLDER_BEGIN + offset;
	temp.setSizeAndValue();
	return temp;
}

ScanVariant::ScanVariant(const size_t &chunkSize, const uint8_t* memory, const ScanVariant &reference)
{
	if (reference.isRange())
	{
		this->type = reference.valueStruct[0].getType();
		ASSERT(
			this->type >= SCAN_VARIANT_ALLTYPES_BEGIN &&
			this->type <= SCAN_VARIANT_ALLTYPES_END
		);
	}
	else if (reference.isPlaceholder())
	{
		this->type = reference.type - SCAN_VARIANT_PLACEHOLDER_BEGIN;
		ASSERT(
			this->type >= SCAN_VARIANT_ALLTYPES_BEGIN &&
			this->type <= SCAN_VARIANT_ALLTYPES_END
		);
	}
	else
	{
		this->type = reference.getType();
		ASSERT((
				this->type >= SCAN_VARIANT_ALLTYPES_BEGIN &&
				this->type <= SCAN_VARIANT_ALLTYPES_END
			) ||
			this->isStructure()
		);
	}

	if (this->type == ScanVariant::SCAN_VARIANT_STRUCTURE)
	{
		size_t offset = 0;
		for (auto member = reference.valueStruct.begin(); member != reference.valueStruct.end(); member++)
		{
			ASSERT(offset + member->getSize() <= chunkSize);

			ScanVariant temp(chunkSize - offset, &memory[offset], *member);
			this->valueStruct.push_back(temp);
			offset += member->getSize();
		}
		return;
	}
	else if (this->type == ScanVariant::SCAN_VARIANT_ASCII_STRING)
	{
		auto sizeInBytes = reference.valueAsciiString.length() * sizeof(std::string::value_type);
		ASSERT(sizeInBytes <= chunkSize);
		this->valueAsciiString =
			std::string(
				(std::string::value_type*)memory,
				(std::string::value_type*)&memory[sizeInBytes]
			);
	}
	else if (this->type == ScanVariant::SCAN_VARIANT_WIDE_STRING)
	{
		auto sizeInBytes = reference.valueWideString.length() * sizeof(std::wstring::value_type);
		ASSERT(sizeInBytes <= chunkSize);
		this->valueWideString =
			std::wstring(
				(std::wstring::value_type*)memory,
				(std::wstring::value_type*)&memory[sizeInBytes]
			);
	}
	else
	{
		auto size = this->getTypeTraits()->getSize();
		ASSERT(size <= chunkSize);
		memcpy(&this->numericValue, &memory[0], size);
	}

	this->setSizeAndValue();
}

ScanVariant::ScanVariant(const ptrdiff_t &value, const ScanVariantType &type)
{
	this->type = type;
	ASSERT(this->type >= SCAN_VARIANT_NUMERICTYPES_BEGIN && this->type <= SCAN_VARIANT_NUMERICTYPES_END);
	
	auto size = this->getTypeTraits()->getSize();
	ASSERT(sizeof(value) >= size);

	memcpy(&this->numericValue, &value, size);
	this->setSizeAndValue();
}

ScanVariant::ScanVariant(const ScanVariant& min, const ScanVariant& max)
{
	ASSERT(min.getType() == max.getType());
	ASSERT(min.getType() >= SCAN_VARIANT_NUMERICTYPES_BEGIN && min.getType() <= SCAN_VARIANT_NUMERICTYPES_END);

	auto offset = min.getType() - SCAN_VARIANT_NUMERICTYPES_BEGIN;
	this->type = SCAN_VARIANT_RANGE_BEGIN + offset;
	valueStruct.push_back(min);
	valueStruct.push_back(max);

	this->setSizeAndValue();
}

ScanVariant::ScanVariant(const MemoryAddress& valueMemoryAddress)
{
	if (sizeof(MemoryAddress) == sizeof(uint32_t))
	{
		this->valueuint32 = reinterpret_cast<uint32_t>(valueMemoryAddress);
		this->type = SCAN_VARIANT_UINT32;
		this->setSizeAndValue();
	}
	else if (sizeof(MemoryAddress) == sizeof(uint64_t))
	{
		this->valueuint64 = reinterpret_cast<uint64_t>(valueMemoryAddress);
		this->type = SCAN_VARIANT_UINT64;
		this->setSizeAndValue();
	}
	else
	{
		// what went wrong? did we change the MemoryAddress type?
		ASSERT(false);
	}
}

const std::wstring ScanVariant::getTypeName() const
{
	return this->getTypeTraits()->getName();
}

const bool ScanVariant::isComposite() const
{
	return this->getTypeTraits()->isStructureType();
}
const std::vector<ScanVariant>& ScanVariant::getCompositeValues() const
{
	return this->valueStruct;
}

const std::wstring ScanVariant::toString() const
{
	auto traits = this->getTypeTraits();
	if (traits->isNumericType())
		return traits->toString((void*)&numericValue);
	else if (traits->isStringType())
	{
		if (this->getType() == SCAN_VARIANT_ASCII_STRING)
			return traits->toString((void*)this->valueAsciiString.c_str());
		else if (this->getType() == SCAN_VARIANT_WIDE_STRING)
			return traits->toString((void*)this->valueWideString.c_str());
	}

	return traits->toString(nullptr);
}

const bool ScanVariant::isNull() const
{
	return (this->type == SCAN_VARIANT_NULL);
}

const bool ScanVariant::getValue(std::string &value) const
{
	if (this->type == SCAN_VARIANT_ASCII_STRING)
	{
		value = valueAsciiString;
		return true;
	}
	return false;
}
const bool ScanVariant::getValue(std::wstring &value) const
{
	if (this->type == SCAN_VARIANT_WIDE_STRING)
	{
		value = valueWideString;
		return true;
	}
	return false;
}
const bool ScanVariant::getValue(uint8_t &value) const
{
	if (this->type == SCAN_VARIANT_UINT8)
	{
		value = valueuint8;
		return true;
	}
	return false;
}
const bool ScanVariant::getValue(int8_t &value) const
{
	if (this->type == SCAN_VARIANT_INT8)
	{
		value = valueint8;
		return true;
	}
	return false;
}
const bool ScanVariant::getValue(uint16_t &value) const
{
	if (this->type == SCAN_VARIANT_UINT16)
	{
		value = valueuint16;
		return true;
	}
	return false;
}
const bool ScanVariant::getValue(int16_t &value) const
{
	if (this->type == SCAN_VARIANT_INT16)
	{
		value = valueint16;
		return true;
	}
	return false;
}
const bool ScanVariant::getValue(uint32_t &value) const
{
	if (this->type == SCAN_VARIANT_UINT32)
	{
		value = valueuint32;
		return true;
	}
	return false;
}
const bool ScanVariant::getValue(int32_t &value) const
{
	if (this->type == SCAN_VARIANT_INT32)
	{
		value = valueint32;
		return true;
	}
	return false;
}
const bool ScanVariant::getValue(uint64_t &value) const
{
	if (this->type == SCAN_VARIANT_UINT64)
	{
		value = valueuint64;
		return true;
	}
	return false;
}
const bool ScanVariant::getValue(int64_t &value) const
{
	if (this->type == SCAN_VARIANT_INT64)
	{
		value = valueint64;
		return true;
	}
	return false;
}
const bool ScanVariant::getValue(double &value) const
{
	if (this->type == SCAN_VARIANT_DOUBLE)
	{
		value = valueDouble;
		return true;
	}
	return false;
}
const bool ScanVariant::getValue(float &value) const
{
	if (this->type == SCAN_VARIANT_FLOAT)
	{
		value = valueFloat;
		return true;
	}
	return false;
}

const bool ScanVariant::getValue(std::vector<ScanVariant> &value) const
{
	if (this->type == SCAN_VARIANT_STRUCTURE)
	{
		value = valueStruct;
		return true;
	}
	return false;
}

void ScanVariant::searchForMatchesInChunk(
		const uint8_t* chunk,
		const size_t &chunkSize,
		const CompareTypeFlags &compType,
		const MemoryAddress &startAddress,
		std::vector<size_t> &locations) const
{
	ASSERT(this->valueSize > 0);
	ASSERT(this->compareToBuffer != nullptr);
	if (chunkSize < this->valueSize) return;

	auto traits = this->getTypeTraits();
	size_t desiredAlignment = traits->getAlignment();
	size_t chunkAlignment = (size_t)startAddress % desiredAlignment;
	size_t startOffset = (chunkAlignment == 0) ? 0 : desiredAlignment - chunkAlignment;
	size_t scanEndAt = chunkSize - this->valueSize;

	auto comp = traits->getComparator();
	auto size = this->valueSize;
	auto numericValue = &this->numericValue;
	auto asciiValue = this->valueAsciiString.c_str();
	auto wideValue = this->valueWideString.c_str();

	for (size_t i = startOffset; i <= scanEndAt; )
	{
		auto res = this->compareToBuffer(
			this, comp, size,
			numericValue,
			asciiValue, wideValue,
			&chunk[i]
		);

		// this won't let us use placeholders unless we're in a structure.
		// to change this needs to check for SCAN_COMPARE_ALWAYS_MATCH
		if ((res & compType) != 0)
		{
			locations.push_back(i);
			i += this->valueSize; // TODO: maybe make overlap checking optional?
		}
		else
			i += desiredAlignment;
	}
}

const CompareTypeFlags ScanVariant::compareRangeToBuffer(
	const ScanVariant* const obj,
	const ScanVariantComparator &comparator,
	const size_t &valueSize,
	const void* const numericBuffer,
	const void* const asciiBuffer,
	const void* const wideBuffer,
	const void* const target)
{
	auto minRes = comparator(&obj->valueStruct[0].numericValue, target);
	if (!(minRes & Scanner::SCAN_COMPARE_GREATER_THAN_OR_EQUALS))
		return Scanner::SCAN_COMPARE_LESS_THAN;
	auto maxRes = comparator(&obj->valueStruct[1].numericValue, target);
	if (!(maxRes & Scanner::SCAN_COMPARE_LESS_THAN_OR_EQUALS))
		return Scanner::SCAN_COMPARE_GREATER_THAN;
	return Scanner::SCAN_COMPARE_EQUALS;
}
const CompareTypeFlags ScanVariant::compareNumericToBuffer(
	const ScanVariant* const obj,
	const ScanVariantComparator &comparator,
	const size_t &valueSize,
	const void* const numericBuffer,
	const void* const asciiBuffer,
	const void* const wideBuffer,
	const void* const target)
{
	return comparator(numericBuffer, target);
}

const CompareTypeFlags ScanVariant::comparePlaceholderToBuffer(
	const ScanVariant* const obj,
	const ScanVariantComparator &comparator,
	const size_t &valueSize,
	const void* const numericBuffer,
	const void* const asciiBuffer,
	const void* const wideBuffer,
	const void* const target)
{
	return Scanner::SCAN_COMPARE_ALWAYS_MATCH;
}
const CompareTypeFlags ScanVariant::compareStructureToBuffer(
	const ScanVariant* const obj,
	const ScanVariantComparator &comparator,
	const size_t &valueSize,
	const void* const numericBuffer,
	const void* const asciiBuffer,
	const void* const wideBuffer,
	const void* const target)
{
	size_t offset = 0;
	auto buf = (uint8_t*)target;
	size_t iterations = obj->valueStruct.size();
	for (size_t i = 0; i < iterations; i++)
	{
		auto innerObj = &obj->valueStruct[i];
		auto res = innerObj->compareTo(&buf[offset]);
		if (!(res & Scanner::SCAN_COMPARE_EQUALS)
			&& !(res & Scanner::SCAN_COMPARE_ALWAYS_MATCH))
		{
			return 0;
		}
		offset += obj->valueStruct[i].getSize();
	}
	return Scanner::SCAN_COMPARE_EQUALS;
}
const CompareTypeFlags ScanVariant::compareAsciiStringToBuffer(
	const ScanVariant* const obj,
	const ScanVariantComparator &comparator,
	const size_t &valueSize,
	const void* const numericBuffer,
	const void* const asciiBuffer,
	const void* const wideBuffer,
	const void* const target)
{
	auto buf = (uint8_t*)target;
	std::string::value_type terminator;
	memcpy(&terminator, &buf[valueSize - sizeof(terminator)], sizeof(terminator));
	if (terminator == (std::string::value_type)0)
	{
		auto res = strcmp((std::string::value_type*)asciiBuffer, (std::string::value_type*)target);
		if (res == 0) return Scanner::SCAN_COMPARE_EQUALS;
		else if (res > 0) return Scanner::SCAN_COMPARE_GREATER_THAN;
		else return Scanner::SCAN_COMPARE_LESS_THAN;
	}
	return 0;
}
const CompareTypeFlags ScanVariant::compareWideStringToBuffer(
	const ScanVariant* const obj,
	const ScanVariantComparator &comparator,
	const size_t &valueSize,
	const void* const numericBuffer,
	const void* const asciiBuffer,
	const void* const wideBuffer,
	const void* const target)
{
	auto buf = (uint8_t*)target;
	std::wstring::value_type terminator;
	memcpy(&terminator, &buf[valueSize - sizeof(terminator)], sizeof(terminator));
	if (terminator == (std::wstring::value_type)0)
	{
		auto res = wcscmp((std::wstring::value_type*)wideBuffer, (std::wstring::value_type*)target);
		if (res == 0) return Scanner::SCAN_COMPARE_EQUALS;
		else if (res > 0) return Scanner::SCAN_COMPARE_GREATER_THAN;
		else return Scanner::SCAN_COMPARE_LESS_THAN;
	}
	return 0;
}

void ScanVariant::setSizeAndValue()
{
	this->compareToBuffer = nullptr;

	// first, we'll set the size and value properly
	auto traits = this->getTypeTraits();
	if (traits->isNumericType())
		this->valueSize = traits->getSize();
	else if (traits->isStringType())
	{
		if (this->getType() == SCAN_VARIANT_ASCII_STRING)
			this->valueSize = this->valueAsciiString.length() + sizeof(std::string::value_type);
		else if (this->getType() == SCAN_VARIANT_WIDE_STRING)
			this->valueSize = this->valueWideString.length() * sizeof(std::wstring::value_type) + sizeof(std::wstring::value_type);
	}
	else if (traits->isStructureType())
	{
		this->valueSize = 0;
		for (auto member = this->valueStruct.begin(); member != this->valueStruct.end(); member++)
			this->valueSize += member->getSize();
	}

	// next, we'll set up the proper comparator
	// we do this to avoid type-checking at compare time.
	// we avoid std:bind and other helps for speed.
	if (this->isRange())
		this->compareToBuffer = &ScanVariant::compareRangeToBuffer;
	else if (this->isPlaceholder())
		this->compareToBuffer = &ScanVariant::comparePlaceholderToBuffer;
	else if (traits->isNumericType())
		this->compareToBuffer = &ScanVariant::compareNumericToBuffer;
	else if (this->getType() == SCAN_VARIANT_ASCII_STRING)
		this->compareToBuffer = &ScanVariant::compareAsciiStringToBuffer;
	else if (this->getType() == SCAN_VARIANT_WIDE_STRING)
		this->compareToBuffer = &ScanVariant::compareWideStringToBuffer;
	else if (traits->isStructureType())
		this->compareToBuffer = &ScanVariant::compareStructureToBuffer;
	else
		ASSERT(false); // didn't find a comparator!
}


ScanVariant ScanVariant::fromString(const std::string &input, const ScanVariantType type)
{
	std::wstring wideString(input.begin(), input.end());
	return ScanVariant::fromString(wideString, type);
}

ScanVariant ScanVariant::fromString(const std::wstring &input, const ScanVariantType type)
{
	ASSERT(type >= SCAN_VARIANT_ALLTYPES_BEGIN && type <= SCAN_VARIANT_ALLTYPES_END);

	auto traits = UnderlyingTypeTraits[type];

	ScanVariant ret;
	traits->fromString(input, ret);
	return ret;
}