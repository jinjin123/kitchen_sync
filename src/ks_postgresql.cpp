#include "endpoint.h"

#include <stdexcept>
#include <set>
#include <libpq-fe.h>

#include "database_client.h"
#include "row_printer.h"

class PostgreSQLRes {
public:
	PostgreSQLRes(PGresult *res);
	~PostgreSQLRes();

	inline PGresult *res() { return _res; }
	inline ExecStatusType status() { return PQresultStatus(_res); }
	inline int n_tuples() const  { return _n_tuples; }
	inline int n_columns() const { return _n_columns; }

private:
	PGresult *_res;
	int _n_tuples;
	int _n_columns;
};

PostgreSQLRes::PostgreSQLRes(PGresult *res) {
	_res = res;
	_n_tuples = PQntuples(_res);
	_n_columns = PQnfields(_res);
}

PostgreSQLRes::~PostgreSQLRes() {
	if (_res) {
		PQclear(_res);
	}
}


class PostgreSQLRow {
public:
	inline PostgreSQLRow(PostgreSQLRes &res, int row_number): _res(res), _row_number(row_number) { }
	inline const PostgreSQLRes &results() const { return _res; }

	inline         int n_columns() const { return _res.n_columns(); }
	inline        bool   null_at(int column_number) const { return PQgetisnull(_res.res(), _row_number, column_number); }
	inline const void *result_at(int column_number) const { return PQgetvalue (_res.res(), _row_number, column_number); }
	inline         int length_of(int column_number) const { return PQgetlength(_res.res(), _row_number, column_number); }
	inline      string string_at(int column_number) const { return string((char *)result_at(column_number), length_of(column_number)); }

private:
	PostgreSQLRes &_res;
	int _row_number;
};


class PostgreSQLClient: public DatabaseClient {
public:
	typedef PostgreSQLRow RowType;

	PostgreSQLClient(
		const char *database_host,
		const char *database_port,
		const char *database_name,
		const char *database_username,
		const char *database_password);
	~PostgreSQLClient();

	template <typename RowReceiver>
	size_t retrieve_rows(const Table &table, const ColumnValues &prev_key, size_t row_count, RowReceiver &row_packer) {
		return query(retrieve_rows_sql(table, prev_key, row_count, '"'), row_packer);
	}

	template <typename RowReceiver>
	size_t retrieve_rows(const Table &table, const ColumnValues &prev_key, const ColumnValues &last_key, RowReceiver &row_packer) {
		return query(retrieve_rows_sql(table, prev_key, last_key, '"'), row_packer);
	}

	size_t count_rows(const Table &table, const ColumnValues &prev_key, const ColumnValues &last_key) {
		return atoi(select_one(count_rows_sql(table, prev_key, last_key, '`')).c_str());
	}

	void execute(const string &sql);
	void disable_referential_integrity();
	void enable_referential_integrity();
	string export_snapshot();
	void import_snapshot(const string &snapshot);
	void unhold_snapshot();
	void start_read_transaction();
	void start_write_transaction();
	void commit_transaction();
	void rollback_transaction();
	string escape_value(const string &value);

	inline const char* replace_sql_prefix() { return "INSERT INTO "; }
	inline char quote_identifiers_with() { return '"'; }

	inline bool need_primary_key_clearer_to_replace() { return true; }

	template <typename UniqueKeyClearerClass>
	void add_replace_clearers(vector<UniqueKeyClearerClass> &unique_key_clearers, const Table &table) {
		for (const Key &key : table.keys) {
			if (key.unique) {
				unique_key_clearers.push_back(UniqueKeyClearerClass(*this, table, key.columns));
			}
		}
	}

protected:
	friend class PostgreSQLTableLister;

	void populate_database_schema();

	template <typename RowFunction>
	size_t query(const string &sql, RowFunction &row_handler) {
		PostgreSQLRes res(PQexecParams(conn, sql.c_str(), 0, NULL, NULL, NULL, NULL, 0 /* text-format results only */));

		if (res.status() != PGRES_TUPLES_OK) {
			backtrace();
			throw runtime_error(PQerrorMessage(conn) + string("\n") + sql);
		}

		for (int row_number = 0; row_number < res.n_tuples(); row_number++) {
			PostgreSQLRow row(res, row_number);
			row_handler(row);
		}

		return res.n_tuples();
	}

	string select_one(const string &sql) {
		PostgreSQLRes res(PQexecParams(conn, sql.c_str(), 0, NULL, NULL, NULL, NULL, 0 /* text-format results only */));

		if (res.status() != PGRES_TUPLES_OK) {
			backtrace();
			throw runtime_error(PQerrorMessage(conn) + string("\n") + sql);
		}

		if (res.n_tuples() != 1 || res.n_columns() != 1) {
			throw runtime_error("Expected query to return only one row with only one column\n" + sql);
		}
		
		return PostgreSQLRow(res, 0).string_at(0);
	}

private:
	PGconn *conn;

	// forbid copying
	PostgreSQLClient(const PostgreSQLClient& copy_from) { throw logic_error("copying forbidden"); }
};

PostgreSQLClient::PostgreSQLClient(
	const char *database_host,
	const char *database_port,
	const char *database_name,
	const char *database_username,
	const char *database_password) {

	const char *keywords[] = { "host",        "port",        "dbname",      "user",            "password",        NULL };
	const char *values[]   = { database_host, database_port, database_name, database_username, database_password, NULL };

	conn = PQconnectdbParams(keywords, values, 1 /* allow expansion */);

	if (PQstatus(conn) != CONNECTION_OK) {
		throw runtime_error(PQerrorMessage(conn));
	}
	if (PQsetClientEncoding(conn, "SQL_ASCII")) {
		throw runtime_error(PQerrorMessage(conn));
	}
}

PostgreSQLClient::~PostgreSQLClient() {
	if (conn) {
		PQfinish(conn);
	}
}

void PostgreSQLClient::execute(const string &sql) {
    PostgreSQLRes res(PQexec(conn, sql.c_str()));

    if (res.status() != PGRES_COMMAND_OK) {
		throw runtime_error(PQerrorMessage(conn) + string("\n") + sql);
    }
}

void PostgreSQLClient::start_read_transaction() {
	execute("START TRANSACTION READ ONLY ISOLATION LEVEL REPEATABLE READ");
	populate_database_schema();
}

void PostgreSQLClient::start_write_transaction() {
	execute("START TRANSACTION ISOLATION LEVEL READ COMMITTED");
	populate_database_schema();
}

void PostgreSQLClient::commit_transaction() {
	execute("COMMIT");
}

void PostgreSQLClient::rollback_transaction() {
	execute("ROLLBACK");
}

string PostgreSQLClient::export_snapshot() {
	// postgresql has transactional DDL, so by starting our transaction before we've even looked at the tables,
	// we'll get a 100% consistent view.
	execute("START TRANSACTION READ ONLY ISOLATION LEVEL REPEATABLE READ");
	populate_database_schema();
	return select_one("SELECT pg_export_snapshot()");
}

void PostgreSQLClient::import_snapshot(const string &snapshot) {
	execute("START TRANSACTION READ ONLY ISOLATION LEVEL REPEATABLE READ");
	execute("SET TRANSACTION SNAPSHOT '" + escape_value(snapshot) + "'");
	populate_database_schema();
}

void PostgreSQLClient::unhold_snapshot() {
	// do nothing - only needed for lock-based systems like mysql
}

void PostgreSQLClient::disable_referential_integrity() {
	execute("SET CONSTRAINTS ALL DEFERRED");

	/* TODO: investigate the pros and cons of disabling triggers - this blocks if there's a read transaction open
	for (Tables::const_iterator table = database.tables.begin(); table != database.tables.end(); ++table) {
		execute("ALTER TABLE " + table->name + " DISABLE TRIGGER ALL");
	}
	*/
}

void PostgreSQLClient::enable_referential_integrity() {
	/* TODO: investigate the pros and cons of disabling triggers - this blocks if there's a read transaction open
	for (Tables::const_iterator table = database.tables.begin(); table != database.tables.end(); ++table) {
		execute("ALTER TABLE " + table->name + " ENABLE TRIGGER ALL");
	}
	*/
}

string PostgreSQLClient::escape_value(const string &value) {
	string result;
	result.resize(value.size()*2 + 1);
	size_t result_length = PQescapeStringConn(conn, (char*)result.data(), value.c_str(), value.size(), NULL);
	result.resize(result_length);
	return result;
}

struct PostgreSQLColumnLister {
	inline PostgreSQLColumnLister(Table &table): table(table) {}

	inline void operator()(PostgreSQLRow &row) {
		Column column(row.string_at(0));
		table.columns.push_back(column);
	}

	Table &table;
};

struct PostgreSQLPrimaryKeyLister {
	inline PostgreSQLPrimaryKeyLister(Table &table): table(table) {}

	inline void operator()(PostgreSQLRow &row) {
		string column_name = row.string_at(0);
		size_t column_index = table.index_of_column(column_name);
		table.primary_key_columns.push_back(column_index);
	}

	Table &table;
};

struct PostgreSQLKeyLister {
	inline PostgreSQLKeyLister(Table &table): table(table) {}

	inline void operator()(PostgreSQLRow &row) {
		// if we have no primary key, we might need to use another unique key as a surrogate - see PostgreSQLTableLister below
		// furthermore this key must have no NULLable columns, as they effectively make the index not unique
		string key_name = row.string_at(0);
		bool unique = (row.string_at(1) == "t");
		string column_name = row.string_at(2);
		size_t column_index = table.index_of_column(column_name);
		// FUTURE: consider representing collation, index type, partial keys etc.

		if (table.keys.empty() || table.keys.back().name != key_name) {
			table.keys.push_back(Key(key_name, unique));
		}
		table.keys.back().columns.push_back(column_index);

		if (table.primary_key_columns.empty()) {
			// if we have no primary key, we might need to use another unique key as a surrogate - see MySQLTableLister below -
			// but this key must have no NULLable columns, as they effectively make the index not unique
			bool nullable = (row.string_at(3) == "f");
			if (unique && nullable) {
				// mark this as unusable
				unique_but_nullable_keys.insert(key_name);
			}
		}
	}

	Table &table;
	set<string> unique_but_nullable_keys;
};

struct PostgreSQLTableLister {
	PostgreSQLTableLister(PostgreSQLClient &client): _client(client) {}

	void operator()(PostgreSQLRow &row) {
		Table table(row.string_at(0));

		PostgreSQLColumnLister column_lister(table);
		_client.query(
			"SELECT attname "
			  "FROM pg_attribute, pg_class "
			 "WHERE attrelid = pg_class.oid AND "
			       "attnum > 0 AND "
			       "NOT attisdropped AND "
			       "relname = '" + table.name + "' "
			 "ORDER BY attnum",
			column_lister);

		PostgreSQLPrimaryKeyLister primary_key_lister(table);
		_client.query(
			"SELECT column_name "
			  "FROM information_schema.table_constraints, "
			       "information_schema.key_column_usage "
			 "WHERE information_schema.table_constraints.table_name = '" + table.name + "' AND "
			       "information_schema.key_column_usage.table_name = information_schema.table_constraints.table_name AND "
			       "constraint_type = 'PRIMARY KEY' "
			 "ORDER BY ordinal_position",
			primary_key_lister);

		PostgreSQLKeyLister key_lister(table);
		_client.query(
			"SELECT index_class.relname, pg_index.indisunique, attname, attnotnull "
			  "FROM pg_class table_class, pg_index, pg_class index_class, generate_subscripts(indkey, 1) AS position, pg_attribute "
			 "WHERE table_class.oid = pg_index.indrelid AND "
			       "pg_index.indexrelid = index_class.oid AND index_class.relkind = 'i' AND "
			       "table_class.oid = pg_attribute.attrelid AND pg_attribute.attnum = indkey[position] AND "
			       "table_class.relname = '" + table.name + "' AND "
			       "NOT pg_index.indisprimary "
			 "ORDER BY relname, position",
			key_lister);

		// if the table has no primary key, we need to find a unique key with no nullable columns to act as a surrogate primary key
		sort(table.keys.begin(), table.keys.end()); // order is arbitrary for keys, but both ends must be consistent, so we sort the keys by name
		
		for (Keys::const_iterator key = table.keys.begin(); key != table.keys.end() && table.primary_key_columns.empty(); ++key) {
			if (key->unique && !key_lister.unique_but_nullable_keys.count(key->name)) {
				table.primary_key_columns = key->columns;
			}
		}
		if (table.primary_key_columns.empty()) {
			// of course this falls apart if there are no unique keys, so we don't allow that
			throw runtime_error("Couldn't find a primary or non-nullable unique key on table " + table.name);
		}

		_client.database.tables.push_back(table);
	}

	PostgreSQLClient &_client;
};

void PostgreSQLClient::populate_database_schema() {
	PostgreSQLTableLister table_lister(*this);
	query("SELECT tablename "
		    "FROM pg_tables "
		   "WHERE schemaname = ANY (current_schemas(false)) "
		   "ORDER BY pg_relation_size(tablename::text) DESC, tablename ASC",
		  table_lister);
	index_database_tables();
}


int main(int argc, char *argv[]) {
	return endpoint_main<PostgreSQLClient>(argc, argv);
}
