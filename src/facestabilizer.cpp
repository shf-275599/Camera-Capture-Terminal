#include "facestabilizer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

void FaceStabilizer::reset()
{
    tracks.clear(); imageSize = QSize(); lastUpdate = -1;
}

std::vector<QRectF> FaceStabilizer::update(const std::vector<QRectF> &detections, const QSize &size, qint64 now)
{
    std::vector<QRectF> boxes;
    for (const Face &face : updateFaces(detections, size, now)) boxes.push_back(face.box);
    return boxes;
}

std::vector<FaceStabilizer::Face> FaceStabilizer::updateFaces(const std::vector<QRectF> &detections, const QSize &size, qint64 now)
{
    if (size != imageSize || now < lastUpdate) reset();
    imageSize = size;
    lastUpdate = now;
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [now](const Track &track) {
        return now - track.seen > 300;
    }), tracks.end());
    for (Track &track : tracks) track.detection = -1;

    struct Match { size_t track, detection; double overlap; };
    std::vector<Match> candidates;
    for (size_t t = 0; t < tracks.size(); ++t) {
        for (size_t d = 0; d < detections.size(); ++d) {
            const QRectF intersection = tracks[t].box.intersected(detections[d]);
            const double area = intersection.width() * intersection.height();
            const double total = tracks[t].box.width() * tracks[t].box.height() +
                    detections[d].width() * detections[d].height() - area;
            const double overlap = total > 0 ? area / total : 0;
            if (overlap >= 0.3) candidates.push_back({t, d, overlap});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Match &a, const Match &b) {
        if (a.overlap != b.overlap) return a.overlap > b.overlap;
        if (a.track != b.track) return a.track < b.track;
        return a.detection < b.detection;
    });
    std::vector<bool> usedTrack(tracks.size(), false), usedDetection(detections.size(), false);
    for (const Match &match : candidates) {
        if (usedTrack[match.track] || usedDetection[match.detection]) continue;
        Track &track = tracks[match.track];
        const QRectF &box = detections[match.detection];
        const double alpha = track.confirmed ? 1.0 - std::exp(-double(now - track.seen) / 60.0) : 1.0;
        track.box = QRectF(track.box.x() + alpha * (box.x() - track.box.x()),
                          track.box.y() + alpha * (box.y() - track.box.y()),
                          track.box.width() + alpha * (box.width() - track.box.width()),
                          track.box.height() + alpha * (box.height() - track.box.height()));
        track.seen = now; track.confirmed = true;
        track.detection = int(match.detection);
        usedTrack[match.track] = true; usedDetection[match.detection] = true;
    }
    // 未确认目标必须连续检出；已确认目标按真实经过时间保留。
    for (size_t t = tracks.size(); t > 0; --t)
        if (!usedTrack[t - 1] && !tracks[t - 1].confirmed) tracks.erase(tracks.begin() + t - 1);
    for (size_t d = 0; d < detections.size(); ++d)
        if (!usedDetection[d]) tracks.push_back({detections[d], now, false, nextId++, int(d)});

    std::vector<Face> visible;
    for (const Track &track : tracks)
        if (track.confirmed && now - track.seen <= 150) visible.push_back({track.box, track.id, track.detection});
    return visible;
}

int runFaceStabilizerTest()
{
    const QSize size(800, 480);
    const QRectF left(100, 100, 80, 80), right(400, 100, 80, 80);
    FaceStabilizer stabilizer;
    bool ok = stabilizer.update({left, right}, size, 0).empty();
    auto boxes = stabilizer.update({right, left}, size, 33);
    ok = ok && boxes.size() == 2 && boxes[0] == left && boxes[1] == right;
    boxes = stabilizer.update({left.translated(10, 0), right}, size, 66);
    ok = ok && boxes.size() == 2 && boxes[0].x() > 100 && boxes[0].x() < 110;
    ok = ok && stabilizer.update({}, size, 216).size() == 2;
    ok = ok && stabilizer.update({}, size, 217).empty();
    ok = ok && stabilizer.update({left}, size, 367).empty();
    ok = ok && stabilizer.update({left}, size, 400).size() == 1;
    ok = ok && stabilizer.update({left}, QSize(640, 480), 433).empty();
    stabilizer.reset();
    ok = ok && stabilizer.update({left}, size, 466).empty();
    stabilizer.update({}, size, 499);
    ok = ok && stabilizer.update({left}, size, 532).empty();
    stabilizer.update({left}, size, 565);
    // 两个候选不能同时更新同一轨迹，未确认的第二个框不显示。
    boxes = stabilizer.update({left, left.translated(5, 0)}, size, 598);
    ok = ok && boxes.size() == 1;
    ok = ok && stabilizer.update({left}, size, 1000).empty();
    stabilizer.reset();
    stabilizer.update({left}, size, 0);
    stabilizer.update({left}, size, 33);
    // 多帧静止附近抖动的输出振幅应小于输入振幅。
    for (int i = 0; i < 10; ++i) {
        boxes = stabilizer.update({left.translated(i % 2 ? -6 : 6, 0)}, size, 66 + i * 33);
        ok = ok && boxes.size() == 1 && std::abs(boxes[0].x() - left.x()) < 6;
    }
    // 重置后不能把上一连接的轨迹直接显示出来。
    stabilizer.reset();
    ok = ok && stabilizer.update({left}, size, 500).empty();
    stabilizer.update({left, right}, size, 533);
    boxes = stabilizer.update({right, left}, size, 566);
    ok = ok && boxes.size() == 2 && boxes[0] == left && boxes[1] == right;
    // 保留期间允许找回已确认框，无须重新等待两次。
    stabilizer.update({}, size, 599);
    ok = ok && stabilizer.update({left, right}, size, 632).size() == 2;
    // 时间倒退同样清空状态，便于显式时间源测试。
    ok = ok && stabilizer.update({left}, size, 0).empty();
    stabilizer.reset();
    const QRectF nearby = left.translated(40, 0);
    stabilizer.update({left, nearby}, size, 0);
    stabilizer.update({left, nearby}, size, 33);
    boxes = stabilizer.update({nearby.translated(-5, 0), left.translated(5, 0)}, size, 66);
    ok = ok && boxes.size() == 2 && boxes[0].x() < 105 && boxes[1].x() > 135;
    stabilizer.reset();
    stabilizer.update({left}, size, 0);
    stabilizer.update({left}, size, 33);
    ok = ok && stabilizer.update({left}, size, 333).size() == 1;
    ok = ok && stabilizer.update({left}, size, 634).empty();
    stabilizer.reset();
    stabilizer.updateFaces({left,right},size,0);
    auto tracked=stabilizer.updateFaces({left,right},size,33);
    const quint64 firstId=tracked.empty()?0:tracked[0].id;
    tracked=stabilizer.updateFaces({right,left},size,66);
    ok=ok && tracked.size()==2 && tracked[0].id==firstId && tracked[0].detection==1 && tracked[1].detection==0;
    tracked=stabilizer.updateFaces({},size,99);
    ok=ok && tracked.size()==2 && tracked[0].detection==-1;
    stabilizer.reset();
    stabilizer.updateFaces({left},size,132);
    tracked=stabilizer.updateFaces({left},size,165);
    ok=ok && tracked.size()==1 && tracked[0].id!=firstId;
    fprintf(stdout, "Face stabilizer: %s\n", ok ? "PASS" : "FAIL"); fflush(stdout);
    return ok ? 0 : 1;
}
