/****************************************************************
 * Copyright (c) 2020-2022 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/
#pragma once

#include <cstdint>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

namespace QtiDataStructure
{

	// From http://www.cse.yorku.ca/~oz/hash.html
	inline uint32_t DjB2Hash(const char *str)
	{
		uint64_t hash = 5381;
		int32_t c;

		while ( (c = *str++) )
			hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

		return hash;
	}

	template <class _Key, class _Data, _Key (*hashFn)(const char*)>
	class HashTable
	{
	private:
		struct Entry
		{
			_Key	key;
			_Data	data;
			Entry*	pNext;
		};

	public:
		class Iterator
		{
		public:
		friend class HashTable;
			Iterator()
				: mRow(0)
				, mpCurrent(nullptr)
			{}

			Iterator(HashTable& source)
				: mSource(source)
				, mRow(0)
				, mpCurrent(nullptr)
			{}

			_Data& Current()
			{
				return mpCurrent->data;
			}

			void Next()
			{
				if (mpCurrent->pNext != nullptr)
				{
					mpCurrent = mpCurrent->pNext;
				}
				else
				{
					while (mRow < mSource.mCapacity && mSource.mTable[++mRow] == nullptr);


					if ( mRow == mSource.mCapacity )
					{
						mpCurrent = nullptr;
					}
					else
					{
						mpCurrent = mSource.mTable[mRow];
					}
				}
			}

			bool End()
			{
				return (mRow == mSource.mCapacity) && (mpCurrent == NULL);
			}

		private:
			HashTable&	mSource;

			uint32_t	mRow;
			Entry*		mpCurrent;
		};

	public:
		HashTable()
			: mTable(nullptr)
			, mCapacity(0)
		{};

		~HashTable()
		{
			Destroy();
		}

		void Destroy()
		{
			for (uint32_t i = 0; i < mCapacity; i++)
			{
				Entry* pEntry = mTable[i];
				while (pEntry)
				{
					Entry* pCurrent = pEntry;
					pEntry = pEntry->pNext;
					if (pCurrent)
						delete pCurrent;
				}
				mTable[i] = 0;
			}
			if (mCapacity > 0)
			{
				if (mTable)
					delete [] mTable;
				mTable = nullptr;
				mCapacity = 0;
			}
		}

		void Init(uint32_t capacity)
		{
			mCapacity = capacity;
			mTable = new Entry*[capacity];

			for (uint32_t i = 0; i < mCapacity; i++)
				mTable[i] = nullptr;
		}

		void Insert(_Key key, _Data& data)
		{
			uint32_t index = key % mCapacity;

			if (mTable[index] == nullptr)
			{
				Entry* pEntry = new Entry;
				mTable[index] = pEntry;
				pEntry->key = key;
				pEntry->data = data;
				pEntry->pNext = nullptr;
			}
			else
			{
				Entry* pEntry = mTable[index];
				while (pEntry->pNext != nullptr)
					pEntry = pEntry->pNext;
				Entry* pNextEntry = new Entry;
				pNextEntry->key = key;
				pNextEntry->data = data;
				pNextEntry->pNext = nullptr;
				pEntry->pNext = pNextEntry;
			}
		}

		void Insert(char const* key, _Data& data)
		{
			_Key hashVal = hashFn(key);
			return Insert( hashVal, data );
		}

		bool Find(_Key key, _Data* result)
		{
			unsigned int index = key % mCapacity;

			Entry* pEntry = mTable[index];
			while (pEntry != nullptr)
			{
				if (pEntry->key == key)
				{
					*result = pEntry->data;
					return true;
				}
				pEntry = pEntry->pNext;
			}
			return false;
		}

		bool Find(char const* key, _Data* result)
		{
			_Key hashVal = hashFn(key);
			return Find(hashVal, result);
		}

		Iterator GetIterator()
		{
			Iterator res(*this);

			unsigned int i =0;

			while (i < mCapacity)
			{
				if (mTable[i] != nullptr)
				{
					res.mpCurrent = mTable[i];
					break;
				}
				i++;
			}
			res.mRow = i;
			return res;
		}

	public:
		Entry**	mTable;
		uint32_t 	mCapacity;
	};

}
