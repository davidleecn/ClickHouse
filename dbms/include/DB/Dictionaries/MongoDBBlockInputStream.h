#pragma once

#include <DB/Core/Block.h>
#include <DB/DataStreams/IProfilingBlockInputStream.h>
#include <DB/DataTypes/DataTypesNumberFixed.h>
#include <DB/DataTypes/DataTypeString.h>
#include <DB/DataTypes/DataTypeDate.h>
#include <DB/DataTypes/DataTypeDateTime.h>
#include <DB/Columns/ColumnString.h>
#include <ext/range.hpp>
#include <mongo/client/dbclient.h>
#include <vector>
#include <string>

namespace DB
{

/// Allows processing results of a MongoDB query as a sequence of Blocks, simplifies chaining
class MongoDBBlockInputStream final : public IProfilingBlockInputStream
{
	enum struct value_type_t
	{
		UInt8,
		UInt16,
		UInt32,
		UInt64,
		Int8,
		Int16,
		Int32,
		Int64,
		Float32,
		Float64,
		String,
		Date,
		DateTime
	};

public:
	MongoDBBlockInputStream(
		std::unique_ptr<mongo::DBClientCursor> cursor_, const Block & sample_block, const std::size_t max_block_size)
		: cursor{std::move(cursor_)}, sample_block{sample_block}, max_block_size{max_block_size}
	{
		if (!cursor->more())
			return;

		types.reserve(sample_block.columns());

		for (const auto idx : ext::range(0, sample_block.columns()))
		{
			const auto & column = sample_block.getByPosition(idx);
			const auto type = column.type.get();

			if (typeid_cast<const DataTypeUInt8 *>(type))
				types.push_back(value_type_t::UInt8);
			else if (typeid_cast<const DataTypeUInt16 *>(type))
				types.push_back(value_type_t::UInt16);
			else if (typeid_cast<const DataTypeUInt32 *>(type))
				types.push_back(value_type_t::UInt32);
			else if (typeid_cast<const DataTypeUInt64 *>(type))
				types.push_back(value_type_t::UInt64);
			else if (typeid_cast<const DataTypeInt8 *>(type))
				types.push_back(value_type_t::Int8);
			else if (typeid_cast<const DataTypeInt16 *>(type))
				types.push_back(value_type_t::Int16);
			else if (typeid_cast<const DataTypeInt32 *>(type))
				types.push_back(value_type_t::Int32);
			else if (typeid_cast<const DataTypeInt64 *>(type))
				types.push_back(value_type_t::Int64);
			else if (typeid_cast<const DataTypeFloat32 *>(type))
				types.push_back(value_type_t::Float32);
			else if (typeid_cast<const DataTypeInt64 *>(type))
				types.push_back(value_type_t::Float64);
			else if (typeid_cast<const DataTypeString *>(type))
				types.push_back(value_type_t::String);
			else if (typeid_cast<const DataTypeDate *>(type))
				types.push_back(value_type_t::Date);
			else if (typeid_cast<const DataTypeDateTime *>(type))
				types.push_back(value_type_t::DateTime);
			else
				throw Exception{
					"Unsupported type " + type->getName(),
					ErrorCodes::UNKNOWN_TYPE
				};

			names.emplace_back(column.name);
		}
	}

	String getName() const override { return "MongoDB"; }

	String getID() const override
	{
		using stream = std::ostringstream;

		return "MongoDB(@" + static_cast<stream &>(stream{} << cursor.get()).str() + ")";
	}

private:
	Block readImpl() override
	{
		if (!cursor->more())
			return {};

		auto block = sample_block.cloneEmpty();

		/// cache pointers returned by the calls to getByPosition
		std::vector<IColumn *> columns(block.columns());
		const auto size = columns.size();

		for (const auto i : ext::range(0, size))
			columns[i] = block.getByPosition(i).column.get();

		std::size_t num_rows = 0;
		while (cursor->more())
		{
			const auto row = cursor->next();

			for (const auto idx : ext::range(0, size))
			{
				const auto value = row[names[idx]];
				if (value.ok())
					insertValue(columns[idx], types[idx], value);
				else
					insertDefaultValue(columns[idx], types[idx]);
			}

			++num_rows;
			if (num_rows == max_block_size)
				break;
		}

		return block;
	}

	static void insertValue(IColumn * const column, const value_type_t type, const mongo::BSONElement & value)
	{
		switch (type)
		{
			case value_type_t::UInt8:
			{
				if (value.type() != mongo::Bool)
					throw Exception{
						"Type mismatch, expected Bool, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnFloat64 *>(column)->insert(value.boolean());
				break;
			}
			case value_type_t::UInt16:
			{
				if (!value.isNumber())
					throw Exception{
						"Type mismatch, expected a number, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnUInt16 *>(column)->insert(value.numberInt());
				break;
			}
			case value_type_t::UInt32:
			{
				if (!value.isNumber())
					throw Exception{
						"Type mismatch, expected a number, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnUInt32 *>(column)->insert(value.numberInt());
				break;
			}
			case value_type_t::UInt64:
			{
				if (!value.isNumber())
					throw Exception{
						"Type mismatch, expected a number, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnUInt64 *>(column)->insert(value.numberLong());
				break;
			}
			case value_type_t::Int8:
			{
				if (!value.isNumber())
					throw Exception{
						"Type mismatch, expected a number, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnInt8 *>(column)->insert(value.numberInt());
				break;
			}
			case value_type_t::Int16:
			{
				if (!value.isNumber())
					throw Exception{
						"Type mismatch, expected a number, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnInt16 *>(column)->insert(value.numberInt());
				break;
			}
			case value_type_t::Int32:
			{
				if (!value.isNumber())
					throw Exception{
						"Type mismatch, expected a number, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnInt32 *>(column)->insert(value.numberInt());
				break;
			}
			case value_type_t::Int64:
			{
				if (!value.isNumber())
					throw Exception{
						"Type mismatch, expected a number, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnInt64 *>(column)->insert(value.numberLong());
				break;
			}
			case value_type_t::Float32:
			{
				if (!value.isNumber())
					throw Exception{
						"Type mismatch, expected a number, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnFloat32 *>(column)->insert(value.number());
				break;
			}
			case value_type_t::Float64:
			{
				if (!value.isNumber())
					throw Exception{
						"Type mismatch, expected a number, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnFloat64 *>(column)->insert(value.number());
				break;
			}
			case value_type_t::String:
			{
				if (value.type() != mongo::String)
					throw Exception{
						"Type mismatch, expected String, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				const auto string = value.String();
				static_cast<ColumnString *>(column)->insertDataWithTerminatingZero(string.data(), string.size() + 1);
				break;
			}
			case value_type_t::Date:
			{
				if (value.type() != mongo::Date)
					throw Exception{
						"Type mismatch, expected Date, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnUInt16 *>(column)->insert(
					UInt16{DateLUT::instance().toDayNum(value.date().toTimeT())});
				break;
			}
			case value_type_t::DateTime:
			{
				if (value.type() != mongo::Date)
					throw Exception{
						"Type mismatch, expected Date, got " + std::string{mongo::typeName(value.type())},
						ErrorCodes::TYPE_MISMATCH
					};

				static_cast<ColumnUInt32 *>(column)->insert(value.date().toTimeT());
				break;
			}
		}
	}

	/// @todo insert default value from the dictionary attribute definition
	static void insertDefaultValue(IColumn * const column, const value_type_t type)
	{
		switch (type)
		{
			case value_type_t::UInt8: static_cast<ColumnUInt8 *>(column)->insertDefault(); break;
			case value_type_t::UInt16: static_cast<ColumnUInt16 *>(column)->insertDefault(); break;
			case value_type_t::UInt32: static_cast<ColumnUInt32 *>(column)->insertDefault(); break;
			case value_type_t::UInt64: static_cast<ColumnUInt64 *>(column)->insertDefault(); break;
			case value_type_t::Int8: static_cast<ColumnInt8 *>(column)->insertDefault(); break;
			case value_type_t::Int16: static_cast<ColumnInt16 *>(column)->insertDefault(); break;
			case value_type_t::Int32: static_cast<ColumnInt32 *>(column)->insertDefault(); break;
			case value_type_t::Int64: static_cast<ColumnInt64 *>(column)->insertDefault(); break;
			case value_type_t::Float32: static_cast<ColumnFloat32 *>(column)->insertDefault(); break;
			case value_type_t::Float64: static_cast<ColumnFloat64 *>(column)->insertDefault(); break;
			case value_type_t::String: static_cast<ColumnString *>(column)->insertDefault(); break;
			case value_type_t::Date: static_cast<ColumnUInt16 *>(column)->insertDefault(); break;
			case value_type_t::DateTime: static_cast<ColumnUInt32 *>(column)->insertDefault(); break;
		}
	}

	std::unique_ptr<mongo::DBClientCursor> cursor;
	Block sample_block;
	const std::size_t max_block_size;
	std::vector<value_type_t> types;
	std::vector<mongo::StringData> names;
};

}
