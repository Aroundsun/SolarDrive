#include "schema.h"

#include <pqxx/pqxx>

namespace solar_metadata {

namespace {

constexpr int kSchemaVersion = 2;

int current_version(pqxx::work& txn) {
    txn.exec(
        "CREATE TABLE IF NOT EXISTS schema_version ("
        "    id INT PRIMARY KEY CHECK (id = 1),"
        "    version INT NOT NULL"
        ")"
    );
    auto r = txn.exec("SELECT version FROM schema_version WHERE id = 1");
    if (r.empty()) {
        return 0;
    }
    return r[0][0].as<int>();
}

void set_version(pqxx::work& txn, int version) {
    txn.exec_params(
        "INSERT INTO schema_version (id, version) VALUES (1, $1) "
        "ON CONFLICT (id) DO UPDATE SET version = EXCLUDED.version",
        version
    );
}

void create_users(pqxx::work& txn) {
    txn.exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            username      TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            storage_quota BIGINT NOT NULL DEFAULT 10737418240,
            used_storage  BIGINT NOT NULL DEFAULT 0,
            created_at    TIMESTAMPTZ NOT NULL DEFAULT NOW()
        )
    )");
    txn.exec("CREATE INDEX IF NOT EXISTS idx_users_username ON users(username)");
}

void drop_file_metadata(pqxx::work& txn) {
    txn.exec("DROP TABLE IF EXISTS file_shares CASCADE");
    txn.exec("DROP TABLE IF EXISTS files CASCADE");
    txn.exec("DROP TABLE IF EXISTS folders CASCADE");
    txn.exec("DROP TABLE IF EXISTS content_objects CASCADE");
}

void create_v2_tables(pqxx::work& txn) {
    txn.exec(R"(
        CREATE TABLE content_objects (
            id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            hash         TEXT NOT NULL,
            size         BIGINT NOT NULL,
            chunk_hashes JSONB NOT NULL,
            mime_type    TEXT NOT NULL DEFAULT 'application/octet-stream',
            created_at   TIMESTAMPTZ NOT NULL DEFAULT NOW()
        )
    )");
    txn.exec(
        "CREATE UNIQUE INDEX idx_content_objects_hash ON content_objects(hash)"
    );

    txn.exec(R"(
        CREATE TABLE folders (
            id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            name       TEXT NOT NULL,
            parent_id  UUID REFERENCES folders(id),
            owner_id   UUID NOT NULL REFERENCES users(id),
            created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
            deleted_at TIMESTAMPTZ
        )
    )");
    txn.exec(R"(
        CREATE UNIQUE INDEX idx_folders_owner_parent_name
        ON folders (
            owner_id,
            COALESCE(parent_id, '00000000-0000-0000-0000-000000000000'::uuid),
            name
        ) WHERE deleted_at IS NULL
    )");
    txn.exec(
        "CREATE INDEX idx_folders_owner_parent "
        "ON folders(owner_id, parent_id) WHERE deleted_at IS NULL"
    );

    txn.exec(R"(
        CREATE TABLE files (
            id         UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            name       TEXT NOT NULL,
            owner_id   UUID NOT NULL REFERENCES users(id),
            folder_id  UUID REFERENCES folders(id),
            content_id UUID NOT NULL REFERENCES content_objects(id),
            created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
            deleted_at TIMESTAMPTZ
        )
    )");
    txn.exec(R"(
        CREATE UNIQUE INDEX idx_files_owner_folder_name
        ON files (
            owner_id,
            COALESCE(folder_id, '00000000-0000-0000-0000-000000000000'::uuid),
            name
        ) WHERE deleted_at IS NULL
    )");
    txn.exec(
        "CREATE INDEX idx_files_owner_folder "
        "ON files(owner_id, folder_id) WHERE deleted_at IS NULL"
    );
    txn.exec(
        "CREATE INDEX idx_files_content_id ON files(content_id) WHERE deleted_at IS NULL"
    );

    txn.exec(R"(
        CREATE TABLE file_shares (
            id              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            file_id         UUID NOT NULL REFERENCES files(id),
            owner_id        UUID NOT NULL REFERENCES users(id),
            share_token     VARCHAR(8) UNIQUE NOT NULL,
            password_hash   TEXT,
            expires_at      TIMESTAMPTZ,
            max_downloads   INT NOT NULL DEFAULT 0,
            download_count  INT NOT NULL DEFAULT 0,
            created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
            is_revoked      BOOLEAN NOT NULL DEFAULT FALSE
        )
    )");
    txn.exec("CREATE INDEX idx_shares_token ON file_shares(share_token)");
    txn.exec("CREATE INDEX idx_shares_owner ON file_shares(owner_id)");
}

} // namespace

bool bootstrap_schema(DbPool& pool) {
    auto guard = pool.acquire();
    pqxx::work txn(*guard);

    create_users(txn);

    const int version = current_version(txn);
    bool migrated = false;
    if (version < kSchemaVersion) {
        drop_file_metadata(txn);
        create_v2_tables(txn);
        set_version(txn, kSchemaVersion);
        migrated = true;
    }

    txn.commit();
    return migrated;
}

} // namespace solar_metadata
