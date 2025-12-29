#pragma once
#include <string>
#include <vector>
#include <algorithm>

namespace fin::utils {

inline int levenshtein_distance(const std::string& s1, const std::string& s2) {
    const size_t m = s1.length();
    const size_t n = s2.length();
    if (m == 0) return n;
    if (n == 0) return m;
    
    std::vector<std::vector<int>> matrix(m + 1, std::vector<int>(n + 1));
    for (size_t i = 0; i <= m; ++i) matrix[i][0] = i;
    for (size_t j = 0; j <= n; ++j) matrix[0][j] = j;
    
    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            matrix[i][j] = std::min({
                matrix[i - 1][j] + 1,
                matrix[i][j - 1] + 1,
                matrix[i - 1][j - 1] + cost
            });
        }
    }
    return matrix[m][n];
}

}