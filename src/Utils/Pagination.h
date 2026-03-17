#pragma once

#include <algorithm>

namespace CT {
namespace Pagination {

struct PageSlice {
    int totalItems   = 0;
    int itemsPerPage = 1;
    int totalPages   = 1;
    int currentPage  = 0;
    int startIndex   = 0;
    int endIndex     = 0;
};

inline int calculateTotalPages(int totalItems, int itemsPerPage) {
    totalItems   = std::max(0, totalItems);
    itemsPerPage = std::max(1, itemsPerPage);
    return std::max(1, (totalItems + itemsPerPage - 1) / itemsPerPage);
}

inline PageSlice makeZeroBasedPageSlice(int totalItems, int itemsPerPage, int requestedPage) {
    totalItems   = std::max(0, totalItems);
    itemsPerPage = std::max(1, itemsPerPage);

    PageSlice slice;
    slice.totalItems   = totalItems;
    slice.itemsPerPage = itemsPerPage;
    slice.totalPages   = calculateTotalPages(totalItems, itemsPerPage);
    slice.currentPage  = std::max(0, std::min(requestedPage, slice.totalPages - 1));
    slice.startIndex   = slice.currentPage * itemsPerPage;
    slice.endIndex     = std::min(slice.startIndex + itemsPerPage, totalItems);
    return slice;
}

inline PageSlice makeOneBasedPageSlice(int totalItems, int itemsPerPage, int requestedPage) {
    totalItems   = std::max(0, totalItems);
    itemsPerPage = std::max(1, itemsPerPage);

    PageSlice slice;
    slice.totalItems   = totalItems;
    slice.itemsPerPage = itemsPerPage;
    slice.totalPages   = calculateTotalPages(totalItems, itemsPerPage);
    slice.currentPage  = std::max(1, std::min(requestedPage, slice.totalPages));
    slice.startIndex   = (slice.currentPage - 1) * itemsPerPage;
    slice.endIndex     = std::min(slice.startIndex + itemsPerPage, totalItems);
    return slice;
}

} // namespace Pagination
} // namespace CT
