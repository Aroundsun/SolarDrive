#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "object_store.h"

using solar_storage::ObjectStore;

namespace fs = std::filesystem;

class ObjectStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "solardrive_object_test";
        fs::remove_all(root_);
        store_ = std::make_unique<ObjectStore>(root_.string(), 4);
    }

    void TearDown() override {
        store_.reset();
        fs::remove_all(root_);
    }

    fs::path root_;
    std::unique_ptr<ObjectStore> store_;
};

TEST(ObjectStoreSha256Test, KnownVector) {
    EXPECT_EQ(
        ObjectStore::sha256("hello"),
        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
}

TEST_F(ObjectStoreTest, PutGetRoundTrip) {
    const std::string data = "SolarDrive content";
    const std::string hash = store_->put(data);

    EXPECT_FALSE(hash.empty());
    EXPECT_TRUE(store_->exists(hash));
    EXPECT_EQ(store_->get(hash), data);
}

TEST_F(ObjectStoreTest, PutDeduplicatesIdenticalContent) {
    const std::string data = "duplicate payload";
    const std::string hash1 = store_->put(data);
    const std::string hash2 = store_->put(data);

    EXPECT_EQ(hash1, hash2);
}

TEST_F(ObjectStoreTest, PutChunkedAndGetChunked) {
    const std::string data(10, 'x');
    const auto hashes = store_->put_chunked(data);

    ASSERT_EQ(hashes.size(), 3u);
    EXPECT_EQ(store_->get_chunked(hashes), data);
}

TEST_F(ObjectStoreTest, RemoveDeletesObject) {
    const std::string hash = store_->put("to-delete");
    ASSERT_TRUE(store_->exists(hash));

    EXPECT_TRUE(store_->remove(hash));
    EXPECT_FALSE(store_->exists(hash));
    EXPECT_FALSE(store_->remove(hash));
}
