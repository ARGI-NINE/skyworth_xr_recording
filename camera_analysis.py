#!/usr/bin/env python3
"""
多相机重投影误差分析工具
- 6 个相机两两组合 (15 对)，计算重投影误差
- RGB 双目基线误差
- 输出自包含 HTML 报告

相机参数定义（由 CameraManager 生成，全 SVR 坐标系）：
- 畸变模型：Kannala-Brandt 鱼眼 (radialDistortion 前 4 个系数非零)
- 外参：Camera→Body/IMU (Tbc), SVR 坐标系
  X_body_svr = R @ X_camera_svr + t
  rotation = 四元数 [x, y, z, w]
  position = 相机原点在 SVR Body 坐标系中的位置
- 内参：QVR/OpenCV 标准约定 (focalX=水平焦距, centerX≈w/2)，直接兼容 OpenCV
- 外参：SVR 约定 (Tbc)，load_cameras() 负责转换为 OpenCV 标准坐标系
"""

import json
import os
import sys
import cv2
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import base64
from io import BytesIO
from itertools import combinations


# ────────────────────── 工具函数 ──────────────────────

def setup_chinese_font():
    paths = [
        '/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc',
        '/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf',
        '/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc',
    ]
    for p in paths:
        if os.path.exists(p):
            from matplotlib import font_manager
            prop = font_manager.FontProperties(fname=p)
            plt.rcParams['font.family'] = 'sans-serif'
            plt.rcParams['font.sans-serif'] = [prop.get_name()] + plt.rcParams['font.sans-serif']
            plt.rcParams['axes.unicode_minus'] = False
            return True
    return False


def quat_to_rotmat(q_xyzw):
    """四元数 (x,y,z,w) → 3x3 旋转矩阵"""
    x, y, z, w = q_xyzw
    return np.array([
        [1 - 2*(y*y + z*z), 2*(x*y - w*z),     2*(x*z + w*y)],
        [2*(x*y + w*z),     1 - 2*(x*x + z*z), 2*(y*z - w*x)],
        [2*(x*z - w*y),     2*(y*z + w*x),     1 - 2*(x*x + y*y)]
    ])


def fig_to_b64(fig):
    buf = BytesIO()
    fig.savefig(buf, format='png', dpi=110, bbox_inches='tight')
    buf.seek(0)
    b = base64.b64encode(buf.read()).decode()
    plt.close(fig)
    return b


# ────────────────────── 相机参数加载 ──────────────────────

def load_cameras(base_dir):
    """加载相机参数，外参由 SVR 转换到 OpenCV 标准坐标系。

    JSON 参数约定（QVR 硬件原生格式 + SVR 外参转换）：
      - 内参 (focalX, focalY, centerX, centerY): QVR/OpenCV 标准约定
        focalX=水平焦距, centerX≈w/2, 可直接作为 OpenCV K 矩阵
      - width/height: 标准像素约定
      - 外参 (position, rotation): SVR 约定 (Tbc, camera→body)

    仅外参需要转换: R = H @ R_svr @ H^T, t = H @ t_svr
    """
    H = np.array([[0, 1, 0], [-1, 0, 0], [0, 0, 1]], dtype=np.float64)

    cameras = {}
    for group in ['rgb', 'ctrl', 'tracking']:
        path = os.path.join(base_dir, f'camera_params_{group}.json')
        with open(path) as f:
            data = json.load(f)
        for cam in data['cameras']:
            name = f"{group}-{cam['eye']}"
            intr = cam['intrinsics']
            extr = cam['extrinsics']

            # 内参 K — JSON 已是 OpenCV 兼容格式，直接使用
            K = np.array([
                [intr['focalX'], 0,             intr['centerX']],
                [0,             intr['focalY'], intr['centerY']],
                [0,             0,             1]
            ], dtype=np.float64)

            D = np.array(intr['radialDistortion'][:4], dtype=np.float64).reshape(1, 4)

            # 外参 SVR → OpenCV 标准坐标系
            R_svr = quat_to_rotmat(extr['rotation'])
            t_svr = np.array(extr['position'], dtype=np.float64)
            R = H @ R_svr @ H.T
            t = H @ t_svr

            cameras[name] = {
                'group': group,
                'eye': cam['eye'],
                'width': cam['width'], 'height': cam['height'],
                'K': K, 'D': D, 'R': R, 't': t,
            }
    return cameras


def relative_transform(cam_a, cam_b):
    """计算相机 A→B 的相对变换 R_BA, t_BA。

    外参 Camera→Body/IMU: X_body = R @ X_camera + t
        X_body = R_A @ X_camA + t_A
        X_body = R_B @ X_camB + t_B

    从 A 到 B:
        R_B @ X_camB + t_B = R_A @ X_camA + t_A
        X_camB = R_B^T @ R_A @ X_camA + R_B^T @ (t_A - t_B)

    =>  R_BA = R_B^T @ R_A
        t_BA = R_B^T @ (t_A - t_B)
    """
    R_BA = cam_b['R'].T @ cam_a['R']
    t_BA = cam_b['R'].T @ (cam_a['t'] - cam_b['t'])
    return R_BA, t_BA


# ────────────────────── 图像处理 ──────────────────────

def undistort_kb(img, K, D):
    """Kannala-Brandt 鱼眼去畸变"""
    h, w = img.shape[:2]
    R = np.eye(3)
    map1, map2 = cv2.fisheye.initUndistortRectifyMap(K, D, R, K, (w, h), cv2.CV_32FC1)
    return cv2.remap(img, map1, map2, cv2.INTER_LINEAR)


def extract_frames(video_path, n=30):
    """均匀采样 n 帧，返回 [(frame_idx, frame), ...]"""
    cap = cv2.VideoCapture(video_path)
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    if total <= 0:
        cap.release()
        return []
    indices = np.linspace(0, total - 1, n, dtype=int)
    frames = []
    for idx in indices:
        cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
        ret, frame = cap.read()
        if ret:
            frames.append((int(idx), frame))
    cap.release()
    return frames


def split_lr(frame):
    """左右眼并排帧 → (left, right)"""
    mid = frame.shape[1] // 2
    return frame[:, :mid], frame[:, mid:]


# ────────────────────── 特征匹配与误差计算 ──────────────────────

def sift_match(gray_a, gray_b, max_pts=3000):
    """SIFT 检测 + BFMatcher + ratio test + 亚像素精炼 → (pts_a, pts_b) 或 (None, None)"""
    sift = cv2.SIFT_create(nfeatures=max_pts)
    kp1, des1 = sift.detectAndCompute(gray_a, None)
    kp2, des2 = sift.detectAndCompute(gray_b, None)
    if des1 is None or des2 is None or len(kp1) < 10 or len(kp2) < 10:
        return None, None, 0
    bf = cv2.BFMatcher(cv2.NORM_L2)
    raw = bf.knnMatch(des1, des2, k=2)
    good = [m for m, n in raw if m.distance < 0.75 * n.distance]
    if len(good) < 8:
        return None, None, len(good)
    pts_a = np.float32([kp1[m.queryIdx].pt for m in good])
    pts_b = np.float32([kp2[m.trainIdx].pt for m in good])

    crit = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_COUNT, 10, 0.01)
    pts_a = pts_a.reshape(-1, 1, 2)
    pts_b = pts_b.reshape(-1, 1, 2)
    cv2.cornerSubPix(gray_a, pts_a, (5, 5), (-1, -1), crit)
    cv2.cornerSubPix(gray_b, pts_b, (5, 5), (-1, -1), crit)
    return pts_a.reshape(-1, 2), pts_b.reshape(-1, 2), len(good)


def reprojection_errors(pts_a, pts_b, cam_a, cam_b):
    """三角化 + 重投影计算误差 (像素)。

    先用 E 矩阵 RANSAC 过滤外点，再三角化。
    返回 (errors_array, n_valid_matches, baseline_est, baseline_gt, angle_error_deg)
    """
    R_BA, t_BA = relative_transform(cam_a, cam_b)
    K_A, K_B = cam_a['K'], cam_b['K']

    # RANSAC 几何验证 — 用 GT F 矩阵的 Sampson 距离过滤外点
    # （不能用数据驱动 E，否则过滤后的点对 GT 几何不自洽）
    E_gt = _skew(t_BA) @ R_BA
    F_gt = np.linalg.inv(K_B).T @ E_gt @ np.linalg.inv(K_A)
    if len(pts_a) >= 15:
        p1h = np.hstack([pts_a, np.ones((len(pts_a), 1))])
        p2h = np.hstack([pts_b, np.ones((len(pts_b), 1))])
        Fp1 = (F_gt @ p1h.T).T
        Ftp2 = (F_gt.T @ p2h.T).T
        sampson_dist = np.sum(p2h * Fp1, axis=1) ** 2 / (
            Fp1[:, 0]**2 + Fp1[:, 1]**2 + Ftp2[:, 0]**2 + Ftp2[:, 1]**2 + 1e-12)
        inlier_mask = sampson_dist < 4.0  # 2px 阈值
        if inlier_mask.sum() >= 8:
            pts_a = pts_a[inlier_mask]
            pts_b = pts_b[inlier_mask]

    if len(pts_a) < 8:
        return np.array([]), 0, 0.0, 0.0, 0.0

    # 投影矩阵
    P_A = K_A @ np.hstack([np.eye(3), np.zeros((3, 1))])
    P_B = K_B @ np.hstack([R_BA, t_BA.reshape(3, 1)])

    # 三角化
    pts_4d = cv2.triangulatePoints(P_A, P_B, pts_a.T, pts_b.T)
    pts_3d = (pts_4d[:3] / pts_4d[3]).T  # Nx3

    # 重投影到相机 B
    proj = (K_B @ (R_BA @ pts_3d.T + t_BA.reshape(3, 1))).T
    proj = proj[:, :2] / proj[:, 2:3]
    errors = np.linalg.norm(proj - pts_b, axis=1)

    # 过滤：正深度 + 去掉极端值
    valid = (pts_4d[3] > 0) & (errors < np.percentile(errors, 95))
    n_valid = int(valid.sum())
    if n_valid < 3:
        return errors, 0, 0.0, 0.0, 0.0

    errors = errors[valid]

    # ── 基线分析（仅对双目有意义） ──
    # recoverPose 返回单位平移，无法恢复尺度。直接用外参基线作为 GT。
    # 用数据驱动 E 恢复的平移方向与 GT 方向的夹角作为外参精度指标。
    baseline_gt = np.linalg.norm(t_BA)
    baseline_est = baseline_gt  # 尺度不可从单位平移恢复，直接使用外参基线
    angle_err = 0.0
    if n_valid >= 15:
        pts_a_v = pts_a[valid]
        pts_b_v = pts_b[valid]
        E, emask = cv2.findEssentialMat(pts_a_v, pts_b_v, K_A,
                                        method=cv2.RANSAC, prob=0.999, threshold=1.0)
        if E is not None and emask is not None:
            emask = emask.ravel().astype(bool)
            if emask.sum() >= 8:
                _, R_est, t_est, _ = cv2.recoverPose(
                    E, pts_a_v[emask], pts_b_v[emask], K_A)
                t_est = t_est.ravel()
                t_gt_dir = t_BA / (np.linalg.norm(t_BA) + 1e-12)
                t_est_dir = t_est / (np.linalg.norm(t_est) + 1e-12)
                cos_a = np.clip(np.dot(t_gt_dir, t_est_dir), -1, 1)
                angle_err = np.degrees(np.arccos(abs(cos_a)))

    return errors, n_valid, baseline_est, baseline_gt, angle_err


# ────────────────────── 立体矫正与极线误差 ──────────────────────

def _skew(v):
    return np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])


def stereo_rectify_pair(cam_l, cam_r):
    """两步法立体矫正：先 KB 去畸变，再标准 stereoRectify。

    返回 (kb_maps_l, kb_maps_r, rect_maps_l, rect_maps_r, P1, P2)
    调用方需先 remap kb 再 remap rect。
    """
    R_BA, t_BA = relative_transform(cam_l, cam_r)
    w, h = cam_l['width'], cam_l['height']
    size = (w, h)
    no_dist = np.zeros((1, 5), dtype=np.float64)

    # KB 去畸变映射
    kb_lx, kb_ly = cv2.fisheye.initUndistortRectifyMap(
        cam_l['K'], cam_l['D'], np.eye(3), cam_l['K'], size, cv2.CV_32FC1)
    kb_rx, kb_ry = cv2.fisheye.initUndistortRectifyMap(
        cam_r['K'], cam_r['D'], np.eye(3), cam_r['K'], size, cv2.CV_32FC1)

    # 去畸变后用原始 K 作为内参（零畸变）
    K_l_ud = cam_l['K'].copy()
    K_r_ud = cam_r['K'].copy()

    # 标准立体矫正（在去畸变空间）
    R1, R2, P1, P2, Q, roi1, roi2 = cv2.stereoRectify(
        K_l_ud, no_dist, K_r_ud, no_dist, size, R_BA, t_BA,
        flags=cv2.CALIB_ZERO_DISPARITY, alpha=0, newImageSize=size)

    rect_lx, rect_ly = cv2.initUndistortRectifyMap(
        K_l_ud, no_dist, R1, P1, size, cv2.CV_32FC1)
    rect_rx, rect_ry = cv2.initUndistortRectifyMap(
        K_r_ud, no_dist, R2, P2, size, cv2.CV_32FC1)

    return (kb_lx, kb_ly), (kb_rx, kb_ry), (rect_lx, rect_ly), (rect_rx, rect_ry), P1, P2


def sampson_distance(F, p1, p2):
    """计算 Sampson 距离（对称传递误差）。"""
    p1h = np.hstack([p1, np.ones((len(p1), 1))])
    p2h = np.hstack([p2, np.ones((len(p2), 1))])
    Fp1 = (F @ p1h.T).T
    Ftp2 = (F.T @ p2h.T).T
    num = np.sum(p2h * Fp1, axis=1) ** 2
    den = Fp1[:, 0]**2 + Fp1[:, 1]**2 + Ftp2[:, 0]**2 + Ftp2[:, 1]**2
    return num / den


def make_rectified_viz(left_rect, right_rect, pts_l, pts_r, n_lines=20):
    """矫正后左右并排图 + 极线 + 匹配点 → base64 JPG"""
    max_w = 1600
    sc = min(1.0, max_w / (left_rect.shape[1] * 2))
    if sc < 1.0:
        left_rect = cv2.resize(left_rect, None, fx=sc, fy=sc)
        right_rect = cv2.resize(right_rect, None, fx=sc, fy=sc)
        pts_l = pts_l * sc
        pts_r = pts_r * sc

    h, w = left_rect.shape[:2]
    canvas = np.zeros((h, w * 2), dtype=np.uint8)
    canvas[:, :w] = left_rect
    canvas[:, w:] = right_rect
    canvas_bgr = cv2.cvtColor(canvas, cv2.COLOR_GRAY2BGR)

    if len(pts_l) > n_lines:
        idx = np.linspace(0, len(pts_l) - 1, n_lines, dtype=int)
    else:
        idx = range(len(pts_l))

    for j in idx:
        y = int(pts_l[j, 1])
        cv2.line(canvas_bgr, (0, y), (w * 2, y), (0, 200, 0), 1)
        cv2.circle(canvas_bgr, (int(pts_l[j, 0]), y), 3, (0, 0, 255), -1)
        cv2.circle(canvas_bgr, (int(pts_r[j, 0]) + w, int(pts_r[j, 1])), 3, (0, 0, 255), -1)

    _, buf = cv2.imencode('.jpg', canvas_bgr, [cv2.IMWRITE_JPEG_QUALITY, 85])
    return base64.b64encode(buf).decode()


def stereo_epipolar_analysis(cam_l, cam_r, vid_frames, grp, n_viz=3):
    """对一组双目做立体矫正 + 极线误差分析。

    矫正后匹配点应在同一水平线上，极线误差 = |y_l - y_r|。
    """
    print(f"   立体矫正: {grp}-left ↔ {grp}-right")
    kb_l, kb_r, rect_l, rect_r, P1, P2 = stereo_rectify_pair(cam_l, cam_r)

    def rectify_lr(left_gray, right_gray):
        """两步 remap: KB去畸变 → 标准立体矫正"""
        ud_l = cv2.remap(left_gray, kb_l[0], kb_l[1], cv2.INTER_LINEAR)
        ud_r = cv2.remap(right_gray, kb_r[0], kb_r[1], cv2.INTER_LINEAR)
        rl = cv2.remap(ud_l, rect_l[0], rect_l[1], cv2.INTER_LINEAR)
        rr = cv2.remap(ud_r, rect_r[0], rect_r[1], cv2.INTER_LINEAR)
        return rl, rr

    all_epi_rect = []      # 矫正后的极线误差 |dy|
    all_epi_data = []      # 数据驱动 F 矩阵的 Sampson 距离
    frame_epi = []
    frame_epi_data = []
    match_counts = []
    viz_imgs = []

    total = len(vid_frames[grp])
    viz_indices = set(np.linspace(0, total - 1, min(n_viz, total), dtype=int))

    for i, (fi, frame) in enumerate(vid_frames[grp]):
        left, right = split_lr(frame)
        if len(left.shape) == 3:
            left = cv2.cvtColor(left, cv2.COLOR_BGR2GRAY)
            right = cv2.cvtColor(right, cv2.COLOR_BGR2GRAY)

        # 也保留仅去畸变的图像（用于 F 矩阵估计）
        ud_l = cv2.remap(left, kb_l[0], kb_l[1], cv2.INTER_LINEAR)
        ud_r = cv2.remap(right, kb_r[0], kb_r[1], cv2.INTER_LINEAR)

        left_rect, right_rect = rectify_lr(left, right)

        # 降采样加速匹配
        scale = 1.0
        if left_rect.shape[1] > 1600:
            scale = 1600.0 / left_rect.shape[1]
            lr = cv2.resize(left_rect, None, fx=scale, fy=scale)
            rr = cv2.resize(right_rect, None, fx=scale, fy=scale)
        else:
            lr, rr = left_rect, right_rect

        pts_l, pts_r, n_match = sift_match(lr, rr)
        match_counts.append(n_match)

        if pts_l is None or len(pts_l) < 8:
            frame_epi.append(float('nan'))
            frame_epi_data.append(float('nan'))
            continue

        pts_l_full = pts_l / scale
        pts_r_full = pts_r / scale

        # ── 矫正后极线误差 |dy| ──
        epi = np.abs(pts_l_full[:, 1] - pts_r_full[:, 1])
        valid = epi < np.percentile(epi, 95)
        n_valid = int(valid.sum())

        # ── 数据驱动 F 矩阵 Sampson 距离（在去畸变图像上） ──
        sc_ud = 1.0
        if ud_l.shape[1] > 1600:
            sc_ud = 1600.0 / ud_l.shape[1]
        ud_l_s = cv2.resize(ud_l, None, fx=sc_ud, fy=sc_ud) if sc_ud < 1 else ud_l
        ud_r_s = cv2.resize(ud_r, None, fx=sc_ud, fy=sc_ud) if sc_ud < 1 else ud_r
        pts_ud_l, pts_ud_r, n_ud = sift_match(ud_l_s, ud_r_s)
        epi_data_val = float('nan')
        if pts_ud_l is not None and len(pts_ud_l) >= 15:
            F, fmask = cv2.findFundamentalMat(pts_ud_l, pts_ud_r, cv2.FM_RANSAC, 1.0, 0.99)
            if F is not None and fmask is not None:
                fmask = fmask.ravel().astype(bool)
                if fmask.sum() >= 10:
                    sd = sampson_distance(F, pts_ud_l[fmask], pts_ud_r[fmask])
                    all_epi_data.extend(sd.tolist())
                    epi_data_val = float(np.mean(sd))

        if n_valid > 0:
            ev = epi[valid]
            all_epi_rect.extend(ev.tolist())
            frame_epi.append(float(np.mean(ev)))
        else:
            frame_epi.append(float('nan'))
        frame_epi_data.append(epi_data_val)

        if i in viz_indices and n_valid > 0:
            viz_imgs.append(make_rectified_viz(left_rect, right_rect, pts_l[valid], pts_r[valid]))

    return {
        'group': grp,
        'epipolar_errors': np.array(all_epi_rect) if all_epi_rect else np.array([]),
        'epipolar_errors_data': np.array(all_epi_data) if all_epi_data else np.array([]),
        'frame_epipolar_means': frame_epi,
        'frame_epipolar_means_data': frame_epi_data,
        'match_counts': match_counts,
        'viz_imgs': viz_imgs,
    }


def compute_esim(cam_l, cam_r, vid_frames):
    """计算外参 E 矩阵与数据 E 矩阵的余弦相似度。"""
    R_BA, t_BA = relative_transform(cam_l, cam_r)
    E_ext = _skew(t_BA) @ R_BA
    E_ext_n = E_ext / (np.linalg.norm(E_ext) + 1e-12)

    K1, K2 = cam_l['K'], cam_r['K']
    D1, D2 = cam_l['D'], cam_r['D']
    w, h = cam_l['width'], cam_l['height']
    ml1, ml2 = cv2.fisheye.initUndistortRectifyMap(K1, D1, np.eye(3), K1, (w,h), cv2.CV_32FC1)
    mr1, mr2 = cv2.fisheye.initUndistortRectifyMap(K2, D2, np.eye(3), K2, (w,h), cv2.CV_32FC1)

    all_pl, all_pr = [], []
    for fi, frame in vid_frames[:5]:
        left, right = split_lr(frame)
        if len(left.shape) == 3:
            left = cv2.cvtColor(left, cv2.COLOR_BGR2GRAY)
            right = cv2.cvtColor(right, cv2.COLOR_BGR2GRAY)
        udl = cv2.remap(left, ml1, ml2, cv2.INTER_LINEAR)
        udr = cv2.remap(right, mr1, mr2, cv2.INTER_LINEAR)
        sc = min(1.0, 1600.0 / udl.shape[1])
        udl_s = cv2.resize(udl, None, fx=sc, fy=sc) if sc < 1 else udl
        udr_s = cv2.resize(udr, None, fx=sc, fy=sc) if sc < 1 else udr
        pl, pr, n = sift_match(udl_s, udr_s)
        if pl is not None and len(pl) >= 10:
            all_pl.append(pl); all_pr.append(pr)

    if not all_pl:
        return 0.0

    all_pl, all_pr = np.vstack(all_pl), np.vstack(all_pr)
    F, mask = cv2.findFundamentalMat(all_pl, all_pr, cv2.FM_RANSAC, 1.0, 0.99)
    if F is None:
        return 0.0

    K1s, K2s = K1 * sc, K2 * sc
    E_data = K2s.T @ F @ K1s
    E_data_n = E_data / (np.linalg.norm(E_data) + 1e-12)

    return abs(np.sum(E_data_n * E_ext_n))


# ────────────────────── HTML 报告生成 ──────────────────────

def generate_report(results, cameras, stereo_results, output_path):
    """生成自包含 HTML 报告"""

    # ── 1. 汇总表 ──
    rows = []
    stereo_rows = []
    cross_rows = []
    for pk, d in results.items():
        e = d['errors']
        is_stereo = d['is_stereo']
        row_class = "stereo" if is_stereo else "cross"
        type_label = "双目" if is_stereo else "跨组"
        avg_match = int(np.mean(d['match_counts'])) if d['match_counts'] else 0
        container = stereo_rows if is_stereo else cross_rows
        if len(e) > 0:
            container.append(
                f'<tr class="{row_class}">'
                f'<td>{pk}</td><td>{type_label}</td>'
                f'<td>{avg_match}</td>'
                f'<td>{np.mean(e):.2f}</td>'
                f'<td>{np.median(e):.2f}</td>'
                f'<td>{np.std(e):.2f}</td>'
                f'<td>{np.max(e):.2f}</td>'
                f'<td>{np.percentile(e, 90):.2f}</td>'
                f'</tr>')
        else:
            container.append(
                f'<tr class="{row_class}">'
                f'<td>{pk}</td><td>{type_label}</td>'
                f'<td>{avg_match}</td>'
                f'<td colspan="5" class="nodata">无有效匹配</td></tr>')
    rows = stereo_rows + cross_rows

    # ── 2. 直方图（每对一个子图，3 列布局） ──
    valid_pairs = [(pk, d) for pk, d in results.items() if len(d['errors']) > 0]
    hist_imgs = {}
    if valid_pairs:
        n = len(valid_pairs)
        cols = 3
        rows_n = (n + cols - 1) // cols
        fig, axes = plt.subplots(rows_n, cols, figsize=(15, 3.5 * rows_n))
        if rows_n == 1:
            axes = np.atleast_2d(axes)
        for idx, (pk, d) in enumerate(valid_pairs):
            ax = axes[idx // cols, idx % cols]
            e = d['errors']
            ax.hist(e, bins=50, color='steelblue', edgecolor='white', alpha=0.85)
            ax.axvline(np.mean(e), color='red', ls='--', lw=1,
                       label=f'均值 {np.mean(e):.2f}')
            ax.axvline(np.median(e), color='orange', ls='--', lw=1,
                       label=f'中位 {np.median(e):.2f}')
            ax.set_title(pk, fontsize=9)
            ax.set_xlabel('px', fontsize=8)
            ax.legend(fontsize=7)
        # 隐藏多余子图
        for j in range(n, rows_n * cols):
            axes[j // cols, j % cols].set_visible(False)
        plt.tight_layout()
        hist_imgs['all'] = fig_to_b64(fig)

    # ── 3. 逐帧趋势图 ──
    trend_imgs = {}

    # 双目趋势
    stereo_keys = [pk for pk, d in results.items() if d['is_stereo'] and any(
        not np.isnan(v) for v in d['frame_errors'])]
    if stereo_keys:
        fig, ax = plt.subplots(figsize=(10, 4))
        for pk in stereo_keys:
            fe = results[pk]['frame_errors']
            xi = [i for i, v in enumerate(fe) if not np.isnan(v)]
            yi = [v for v in fe if not np.isnan(v)]
            if xi:
                ax.plot(xi, yi, marker='o', ms=3, label=pk)
        ax.set_xlabel('帧序号')
        ax.set_ylabel('平均重投影误差 (px)')
        ax.set_title('双目相机逐帧重投影误差')
        ax.legend(fontsize=8)
        ax.grid(True, alpha=0.3)
        plt.tight_layout()
        trend_imgs['stereo'] = fig_to_b64(fig)

    # 跨组趋势（分页，每页最多 6 条曲线）
    cross_keys = [pk for pk, d in results.items() if not d['is_stereo'] and any(
        not np.isnan(v) for v in d['frame_errors'])]
    for page, start in enumerate(range(0, max(len(cross_keys), 1), 6)):
        batch = cross_keys[start:start + 6]
        if not batch:
            continue
        fig, ax = plt.subplots(figsize=(10, 4))
        has = False
        for pk in batch:
            fe = results[pk]['frame_errors']
            xi = [i for i, v in enumerate(fe) if not np.isnan(v)]
            yi = [v for v in fe if not np.isnan(v)]
            if xi:
                has = True
                ax.plot(xi, yi, marker='o', ms=2, label=pk)
        if has:
            ax.set_xlabel('帧序号')
            ax.set_ylabel('平均重投影误差 (px)')
            ax.set_title(f'跨组相机逐帧重投影误差 ({page + 1})')
            ax.legend(fontsize=7)
            ax.grid(True, alpha=0.3)
            plt.tight_layout()
            trend_imgs[f'cross_{page}'] = fig_to_b64(fig)
        else:
            plt.close(fig)

    # ── 4. RGB 基线板块 ──
    rgb_key = None
    for pk, d in results.items():
        if 'rgb-left' in pk and 'rgb-right' in pk:
            rgb_key = pk
            break

    baseline_gt_mm = 0.0
    baseline_angle_err = 0.0
    if rgb_key:
        rd = results[rgb_key]
        bl_vec = cameras['rgb-left']['t'] - cameras['rgb-right']['t']
        baseline_gt_mm = np.linalg.norm(bl_vec) * 1000
        baseline_angle_err = rd.get('angle_err', 0.0)

    # ── 5. 相机参数表 ──
    param_rows = []
    for name, c in cameras.items():
        param_rows.append(
            f'<tr><td>{name}</td>'
            f'<td>{c["width"]}x{c["height"]}</td>'
            f'<td>{c["K"][0,0]:.2f} / {c["K"][1,1]:.2f}</td>'
            f'<td>({c["K"][0,2]:.1f}, {c["K"][1,2]:.1f})</td>'
            f'<td>[{c["D"][0,0]:.4f}, {c["D"][0,1]:.4f}, {c["D"][0,2]:.4f}, {c["D"][0,3]:.4f}]</td>'
            f'<td>({c["t"][0]:.4f}, {c["t"][1]:.4f}, {c["t"][2]:.4f})</td>'
            f'</tr>')

    # ── 组装 HTML ──
    hist_html = ''.join(
        f'<div class="chart"><img src="data:image/png;base64,{b}" /></div>'
        for b in hist_imgs.values())
    trend_html = ''.join(
        f'<div class="chart"><img src="data:image/png;base64,{b}" /></div>'
        for b in trend_imgs.values())

    baseline_section = ''
    if rgb_key:
        rd = results[rgb_key]
        ang_str = f'{baseline_angle_err:.2f}°' if baseline_angle_err > 0 else 'N/A'
        baseline_section = f'''
<div class="section baseline">
<h2>RGB 双目基线分析</h2>
<table>
<tr><th>指标</th><th>值</th></tr>
<tr><td>外参基线距离</td><td>{baseline_gt_mm:.2f} mm</td></tr>
<tr><td>基线方向角度误差</td><td>{ang_str}</td></tr>
</table>
<p>基线距离来自外参 (recoverPose 无法恢复尺度)。角度误差 = 数据驱动平移方向与 GT 平移方向的夹角，越小表示外参越准确。</p>
</div>'''

    # ── 6. 立体矫正 + 极线误差 ──
    stereo_section = ''
    if stereo_results:
        # 汇总表（包含外参矫正和数据驱动两种指标）
        stereo_rows_html = ''
        for sr in stereo_results.values():
            epi = sr['epipolar_errors']
            epi_d = sr['epipolar_errors_data']
            grp = sr['group'].upper()
            avg_m = int(np.mean(sr['match_counts'])) if sr['match_counts'] else 0

            # 矫正极线误差行
            if len(epi) > 0:
                stereo_rows_html += (
                    f'<tr><td>{grp} 矫正 |Δy|</td><td>{avg_m}</td>'
                    f'<td>{np.mean(epi):.2f}</td>'
                    f'<td>{np.median(epi):.2f}</td>'
                    f'<td>{np.std(epi):.2f}</td>'
                    f'<td>{np.max(epi):.2f}</td>'
                    f'<td>{np.percentile(epi, 90):.2f}</td></tr>')
            else:
                stereo_rows_html += (
                    f'<tr><td>{grp} 矫正 |Δy|</td><td>{avg_m}</td>'
                    f'<td colspan="5" class="nodata">无有效匹配</td></tr>')

            # F 矩阵 Sampson 距离行
            if len(epi_d) > 0:
                stereo_rows_html += (
                    f'<tr><td>{grp} F Sampson</td><td>{avg_m}</td>'
                    f'<td>{np.mean(epi_d):.4f}</td>'
                    f'<td>{np.median(epi_d):.4f}</td>'
                    f'<td>{np.std(epi_d):.4f}</td>'
                    f'<td>{np.max(epi_d):.4f}</td>'
                    f'<td>{np.percentile(epi_d, 90):.4f}</td></tr>')
            else:
                stereo_rows_html += (
                    f'<tr><td>{grp} F Sampson</td><td>{avg_m}</td>'
                    f'<td colspan="5" class="nodata">无有效匹配</td></tr>')

            # E 矩阵相似度行
            e_sim = sr.get('e_sim', 0)
            status = '✓ 一致' if e_sim > 0.8 else ('⚠ 有误差' if e_sim > 0.5 else '✗ 不匹配')
            stereo_rows_html += (
                f'<tr><td>{grp} E_sim</td><td colspan="6"><b>{e_sim:.3f}</b>  {status}</td></tr>')

        # 极线误差直方图
        epi_hist_b64 = ''
        valid_epi = [(g, sr['epipolar_errors']) for g, sr in stereo_results.items()
                     if len(sr['epipolar_errors']) > 0]
        if valid_epi:
            n = len(valid_epi)
            fig, axes = plt.subplots(1, n, figsize=(6 * n, 3.5))
            if n == 1:
                axes = [axes]
            for ax, (g, epi) in zip(axes, valid_epi):
                ax.hist(epi, bins=50, color='#1976D2', edgecolor='white', alpha=0.85)
                ax.axvline(np.mean(epi), color='red', ls='--', lw=1,
                           label=f'均值 {np.mean(epi):.2f}')
                ax.axvline(np.median(epi), color='orange', ls='--', lw=1,
                           label=f'中位 {np.median(epi):.2f}')
                ax.set_title(f'{g.upper()} 极线误差', fontsize=10)
                ax.set_xlabel('|Δy| (px)', fontsize=8)
                ax.legend(fontsize=7)
            plt.tight_layout()
            epi_hist_b64 = fig_to_b64(fig)

        # 逐帧极线误差趋势
        epi_trend_b64 = ''
        has_trend = any(any(not np.isnan(v) for v in sr['frame_epipolar_means'])
                        for sr in stereo_results.values())
        if has_trend:
            fig, ax = plt.subplots(figsize=(10, 4))
            for g, sr in stereo_results.items():
                fe = sr['frame_epipolar_means']
                xi = [i for i, v in enumerate(fe) if not np.isnan(v)]
                yi = [v for v in fe if not np.isnan(v)]
                if xi:
                    ax.plot(xi, yi, marker='o', ms=3, label=g.upper())
            ax.set_xlabel('帧序号')
            ax.set_ylabel('平均极线误差 |Δy| (px)')
            ax.set_title('双目立体矫正后逐帧极线误差')
            ax.legend(fontsize=8)
            ax.grid(True, alpha=0.3)
            plt.tight_layout()
            epi_trend_b64 = fig_to_b64(fig)

        # 矫正可视化图
        viz_html = ''
        for g, sr in stereo_results.items():
            for img_b64 in sr['viz_imgs']:
                viz_html += (
                    f'<div class="chart">'
                    f'<p>{g.upper()} 矫正后极线可视化（绿线=极线，红点=匹配点）</p>'
                    f'<img src="data:image/jpeg;base64,{img_b64}" />'
                    f'</div>')

        epi_hist_html = (f'<div class="chart">'
                         f'<img src="data:image/png;base64,{epi_hist_b64}" /></div>'
                         ) if epi_hist_b64 else ''
        epi_trend_html = (f'<div class="chart">'
                          f'<img src="data:image/png;base64,{epi_trend_b64}" /></div>'
                          ) if epi_trend_b64 else ''

        stereo_section = f'''
<div class="section stereo-rect">
<h2>立体矫正极线误差分析</h2>
<p>两组指标：<br>
<b>矫正 |Δy|</b>: KB去畸变 + 标准stereoRectify后，匹配点垂直距离 |y<sub>L</sub> - y<sub>R</sub>|，理想值≈0。<br>
<b>F Sampson</b>: 在去畸变图像上用数据驱动估计 F 矩阵，计算 Sampson 距离，反映实际对极约束质量。</p>
<p><b>外参精度诊断 (E_sim)</b>: 外参推导的本质矩阵 E 与数据估计的 E 之间的余弦相似度。<br>
E_sim→1 表示外参与观测一致；E_sim&lt;0.5 表示外参可能有误。</p>
<table>
<tr><th>双目组</th><th>平均匹配点</th><th>均值 (px)</th><th>中位数 (px)</th><th>标准差 (px)</th><th>最大值 (px)</th><th>P90 (px)</th></tr>
{stereo_rows_html}
</table>
{epi_hist_html}
{epi_trend_html}
{viz_html}
</div>'''

    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>相机重投影误差分析报告</title>
<style>
body{{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;max-width:1200px;margin:0 auto;padding:20px;background:#f5f5f5;color:#333}}
h1{{color:#333;border-bottom:3px solid #2e7d32;padding-bottom:10px}}
h2{{color:#444;margin-top:28px}}
table{{border-collapse:collapse;width:100%;margin:12px 0;background:#fff;box-shadow:0 1px 4px rgba(0,0,0,.08)}}
th,td{{border:1px solid #ddd;padding:7px 11px;text-align:center;font-size:13px}}
th{{background:#2e7d32;color:#fff;font-weight:600}}
tr:nth-child(even){{background:#f9f9f9}}
tr:hover{{background:#e8f5e9}}
.stereo{{border-left:4px solid #4CAF50}}
.cross{{border-left:4px solid #1976D2}}
.baseline{{border-left:4px solid #FF9800}}
.stereo-rect{{border-left:4px solid #7B1FA2}}
.section{{background:#fff;padding:18px 22px;margin:14px 0;box-shadow:0 1px 4px rgba(0,0,0,.08);border-radius:4px}}
.chart{{background:#fff;padding:10px;margin:10px 0;text-align:center;box-shadow:0 1px 4px rgba(0,0,0,.08)}}
.chart img{{max-width:100%}}
.nodata{{color:#999;font-style:italic}}
</style>
</head>
<body>
<h1>相机重投影误差分析报告</h1>
<p>采样帧数: 30 / 视频 &nbsp;|&nbsp; 特征: SIFT &nbsp;|&nbsp; 畸变模型: Kannala-Brandt</p>

<div class="section">
<h2>相机参数概览</h2>
<table>
<tr><th>相机</th><th>分辨率</th><th>焦距 (fx/fy)</th><th>光心 (cx,cy)</th><th>KB 畸变 [k1..k4]</th><th>位置 (m)</th></tr>
{''.join(param_rows)}
</table>
</div>

<div class="section">
<h2>重投影误差汇总</h2>
<table>
<tr><th>相机对</th><th>类型</th><th>平均匹配点</th><th>均值 (px)</th><th>中位数 (px)</th><th>标准差 (px)</th><th>最大值 (px)</th><th>P90 (px)</th></tr>
{''.join(rows)}
</table>
</div>

{baseline_section}

{stereo_section}

<h2>误差分布直方图</h2>
{hist_html}

<h2>逐帧误差趋势</h2>
{trend_html}

<p style="color:#999;text-align:center;margin-top:30px">自动生成 by camera_analysis.py</p>
</body></html>"""

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(html)
    print(f"报告已保存: {output_path}")


# ────────────────────── 主流程 ──────────────────────

def main():
    base_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    setup_chinese_font()

    print("1. 加载相机参数...")
    cameras = load_cameras(base_dir)
    for name, c in cameras.items():
        print(f"   {name}: {c['width']}x{c['height']}, "
              f"f=({c['K'][0,0]:.1f},{c['K'][1,1]:.1f}), "
              f"pos=({c['t'][0]:.4f},{c['t'][1]:.4f},{c['t'][2]:.4f})")

    print("\n2. 提取视频帧...")
    vid_frames = {}
    for grp in ['rgb', 'ctrl', 'tracking']:
        p = os.path.join(base_dir, f'{grp}.mp4')
        vid_frames[grp] = extract_frames(p, n=30)
        print(f"   {grp}.mp4 → {len(vid_frames[grp])} 帧")

    # 预计算每帧的去畸变图像（避免重复计算）
    print("\n3. 预处理去畸变图像...")
    undist_cache = {}  # key: (group, eye, frame_idx) → gray undistorted
    for grp in ['rgb', 'ctrl', 'tracking']:
        cam_l = cameras[f'{grp}-left']
        cam_r = cameras[f'{grp}-right']
        for fi, frame in vid_frames[grp]:
            left, right = split_lr(frame)
            # 灰度化
            if len(left.shape) == 3:
                left = cv2.cvtColor(left, cv2.COLOR_BGR2GRAY)
                right = cv2.cvtColor(right, cv2.COLOR_BGR2GRAY)
            # 去畸变
            undist_cache[(grp, 'left', fi)] = undistort_kb(left, cam_l['K'], cam_l['D'])
            undist_cache[(grp, 'right', fi)] = undistort_kb(right, cam_r['K'], cam_r['D'])
    print(f"   预处理完成: {len(undist_cache)} 张图")

    # 生成所有 15 对
    cam_names = sorted(cameras.keys())
    pairs = list(combinations(cam_names, 2))
    print(f"\n4. 计算 {len(pairs)} 对相机的重投影误差...")

    results = {}
    for cam_a_name, cam_b_name in pairs:
        cam_a = cameras[cam_a_name]
        cam_b = cameras[cam_b_name]
        grp_a, eye_a = cam_a_name.split('-')
        grp_b, eye_b = cam_b_name.split('-')
        is_stereo = (grp_a == grp_b and eye_a != eye_b)

        pair_key = f"{cam_a_name} ↔ {cam_b_name}"
        all_errors = []
        frame_errors = []
        match_counts = []
        angle_err_acc = []

        # 帧对齐：使用相同索引（视频时长接近，均匀采样）
        n = min(len(vid_frames[grp_a]), len(vid_frames[grp_b]))
        for i in range(n):
            fi_a = vid_frames[grp_a][i][0]
            fi_b = vid_frames[grp_b][i][0]

            img_a = undist_cache.get((grp_a, eye_a, fi_a))
            img_b = undist_cache.get((grp_b, eye_b, fi_b))

            if img_a is None or img_b is None:
                frame_errors.append(float('nan'))
                match_counts.append(0)
                continue

            # 对大图降采样以加速匹配（RGB 2328x1748 → ~1164x874）
            scale_a = 1.0
            scale_b = 1.0
            if img_a.shape[1] > 1600:
                scale_a = 1600.0 / img_a.shape[1]
                img_a = cv2.resize(img_a, None, fx=scale_a, fy=scale_a)
            if img_b.shape[1] > 1600:
                scale_b = 1600.0 / img_b.shape[1]
                img_b = cv2.resize(img_b, None, fx=scale_b, fy=scale_b)

            pts_a, pts_b, n_match = sift_match(img_a, img_b)
            match_counts.append(n_match)

            if pts_a is None or len(pts_a) < 8:
                frame_errors.append(float('nan'))
                continue

            # 还原坐标到原始分辨率
            pts_a = pts_a / scale_a
            pts_b = pts_b / scale_b

            errors, n_valid, bl_est, bl_gt, ang_err = reprojection_errors(
                pts_a, pts_b, cam_a, cam_b)

            if n_valid > 0:
                all_errors.extend(errors.tolist())
                frame_errors.append(float(np.mean(errors)))
                if ang_err > 0:
                    angle_err_acc.append(ang_err)
            else:
                frame_errors.append(float('nan'))

        errors_arr = np.array(all_errors) if all_errors else np.array([])
        results[pair_key] = {
            'cam_a': cam_a_name,
            'cam_b': cam_b_name,
            'errors': errors_arr,
            'frame_errors': frame_errors,
            'match_counts': match_counts,
            'is_stereo': is_stereo,
            'baseline_est': bl_est,  # = baseline_gt, 来自外参
            'angle_err': float(np.median(angle_err_acc)) if angle_err_acc else 0.0,
        }

        if len(errors_arr) > 0:
            print(f"   {pair_key}: mean={np.mean(errors_arr):.2f}px  "
                  f"med={np.median(errors_arr):.2f}px  "
                  f"matches≈{int(np.mean(match_counts))}")
        else:
            print(f"   {pair_key}: 无有效匹配")

    # ── 立体矫正极线误差（3 组双目） ──
    print("\n5. 立体矫正 + 极线误差分析...")
    stereo_results = {}
    for grp in ['rgb', 'ctrl', 'tracking']:
        cam_l = cameras[f'{grp}-left']
        cam_r = cameras[f'{grp}-right']
        stereo_results[grp] = stereo_epipolar_analysis(cam_l, cam_r, vid_frames, grp)
        sr = stereo_results[grp]
        epi = sr['epipolar_errors']
        epi_d = sr['epipolar_errors_data']
        if len(epi) > 0:
            print(f"   {grp}: 矫正极线 mean={np.mean(epi):.2f}px  med={np.median(epi):.2f}px  "
                  f"matches≈{int(np.mean(sr['match_counts']))}")
        else:
            print(f"   {grp}: 矫正极线 - 无有效匹配")
        if len(epi_d) > 0:
            print(f"   {grp}: F矩阵 Sampson mean={np.mean(epi_d):.4f}px  med={np.median(epi_d):.4f}px")
        else:
            print(f"   {grp}: F矩阵 - 无有效匹配")

        # E 矩阵相似度
        e_sim = compute_esim(cam_l, cam_r, vid_frames[grp])
        sr['e_sim'] = e_sim
        print(f"   {grp}: E_sim = {e_sim:.4f}")

    # 生成报告
    print("\n6. 生成 HTML 报告...")
    output_path = os.path.join(base_dir, 'camera_analysis_report.html')
    generate_report(results, cameras, stereo_results, output_path)


if __name__ == '__main__':
    main()
