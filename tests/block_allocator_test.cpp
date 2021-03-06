#include "catch.hpp"
#include "nicegraf_internal.h"
#include <random>

struct test_data {
  uint8_t b1;
  float f1;
  void *p1, *p2;
};

static constexpr uint32_t num_max_entries = 1000u;

TEST_CASE("Basic block allocator functionality", "[blkalloc_basic]") {
  _ngf_block_allocator *allocator =
      _ngf_blkalloc_create(sizeof(test_data), num_max_entries);
  REQUIRE(allocator != NULL);
  test_data *data[num_max_entries] = {nullptr};
  for (uint32_t i = 0u; i < num_max_entries; ++i) {
    data[i] = (test_data*)_ngf_blkalloc_alloc(allocator);
    REQUIRE(data[i] != NULL);
    data[i]->p2 = (void*)data[i];
  }
  // Max. number of entries reached, try adding a pool.
  test_data *null_blk = (test_data*)_ngf_blkalloc_alloc(allocator);
  REQUIRE(null_blk != NULL);
  for (uint32_t i = 0u; i < num_max_entries; ++i) {
    REQUIRE(data[i]->p2 == (void*)data[i]);
    _ngf_blkalloc_error err = _ngf_blkalloc_free(allocator, data[i]);
    REQUIRE(err == _NGF_BLK_NO_ERROR);
  }
  test_data *blk = (test_data*)_ngf_blkalloc_alloc(allocator);
  REQUIRE(blk != NULL);
  _ngf_block_allocator *alloc2 =
      _ngf_blkalloc_create(sizeof(test_data), num_max_entries);
  _ngf_blkalloc_error err = _ngf_blkalloc_free(alloc2, blk);
  REQUIRE(err == _NGF_BLK_WRONG_ALLOCATOR);
  err = _ngf_blkalloc_free(allocator, blk);
  REQUIRE(err == _NGF_BLK_NO_ERROR);
  err = _ngf_blkalloc_free(allocator, blk);
  REQUIRE(err == _NGF_BLK_DOUBLE_FREE);
  _ngf_blkalloc_destroy(allocator);
  _ngf_blkalloc_destroy(alloc2);
}

TEST_CASE("Block allocator fuzz test", "[blkalloc_fuzz]") {
  constexpr uint32_t ntests = 30000u;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> d(0.0, 1.0);
  uint32_t active_blocks = 0u;
  test_data *data[num_max_entries] = {nullptr};
  _ngf_block_allocator *allocator =
      _ngf_blkalloc_create(sizeof(test_data), num_max_entries);
  for (uint32_t t = 0u; t < ntests; ++t) {
    bool allocate = d(gen) < 0.5f;
    if (allocate) {
      test_data *x = (test_data*)_ngf_blkalloc_alloc(allocator);
      if (active_blocks == num_max_entries) {
        REQUIRE(x != NULL);
        _ngf_blkalloc_free(allocator, x);
      }
      else {
        REQUIRE(x != NULL);
        uint32_t idx = 0;
        while(data[idx] != NULL && idx++ < num_max_entries);
        REQUIRE(idx < num_max_entries);
        data[idx] = x;
        x->p1 = (test_data*)x;
        active_blocks++;
      }
    } else if (active_blocks > 0u) {
      uint32_t idx = 0u;
      idx = (num_max_entries - 1u) * d(gen);
      if (data[idx] != nullptr) {
        --active_blocks;
        REQUIRE(data[idx]->p1 == (test_data*)data[idx]);
        _ngf_blkalloc_error err = _ngf_blkalloc_free(allocator, data[idx]);
        REQUIRE(err == _NGF_BLK_NO_ERROR);
        data[idx] = nullptr;
      }
    }
  }
}
