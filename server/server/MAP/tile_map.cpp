#include "pch.h"
#include "tile_map.h"

TILE_MAP tile_map[MAP_WIDTH][MAP_HEIGHT];

#include <queue>
#include <vector>
#include <tuple>

// 4방향 (상하좌우)
const int dd[4][2] = { {0,1},{1,0},{0,-1},{-1,0} };

std::pair<int, int> astar_next_move(int sx, int sy, int tx, int ty) {
    struct Node {
        int x, y, g, f;
        Node* from;
        bool operator<(const Node& o) const { return f > o.f; }
    };
    std::priority_queue<Node> pq;
    std::vector<std::vector<bool>> closed(MAP_WIDTH, std::vector<bool>(MAP_HEIGHT, false));

    pq.push({ sx, sy, 0, abs(tx - sx) + abs(ty - sy), nullptr });

    while (!pq.empty()) {
        Node cur = pq.top(); pq.pop();

        if (cur.x == tx && cur.y == ty) {
            // 목표 도착 → 첫 이동칸 역추적
            Node* n = &cur;
            while (n->from && !(n->from->x == sx && n->from->y == sy)) n = n->from;
            return { n->x, n->y };
        }
        closed[cur.x][cur.y] = true;

        for (int d = 0; d < 4; ++d) {
            int nx = cur.x + dd[d][0], ny = cur.y + dd[d][1];
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;

            // *** 장애물 체크는 아래 한 줄로 충분 ***
            if (tile_map[nx][ny].state) continue;

            if (closed[nx][ny]) continue;

            Node* prev = new Node(cur); // (메모리 누수는 루프작업시에는 무시)
            pq.push({ nx, ny, cur.g + 1, cur.g + 1 + abs(tx - nx) + abs(ty - ny), prev });
        }
    }
    return { sx, sy }; // 경로를 못 찾으면 제자리 반환
}
