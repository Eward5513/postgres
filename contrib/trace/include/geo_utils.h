#ifndef TRACE_GEO_UTILS_H
#define TRACE_GEO_UTILS_H

#include <cmath>
#include "trace.h" // for SimpleBounds, SimplePoint

// Mean Earth radius in meters
static constexpr double TRACE_EARTH_RADIUS_M = 6371008.8;

// Great-circle horizontal distance (meters) between two lon/lat points in degrees
static inline double haversine_horizontal_meters(double lon0_deg, double lat0_deg,
                                                double lon1_deg, double lat1_deg)
{
    double lat0 = lat0_deg * M_PI / 180.0;
    double lon0 = lon0_deg * M_PI / 180.0;
    double lat1 = lat1_deg * M_PI / 180.0;
    double lon1 = lon1_deg * M_PI / 180.0;
    double dlat = lat1 - lat0;
    double dlon = lon1 - lon0;
    double sin_dlat2 = std::sin(dlat * 0.5);
    double sin_dlon2 = std::sin(dlon * 0.5);
    double a = sin_dlat2 * sin_dlat2 + std::cos(lat0) * std::cos(lat1) * sin_dlon2 * sin_dlon2;
    double cang = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
    return TRACE_EARTH_RADIUS_M * cang;
}

// 3D distance squared in meters between (lon,lat,z) and (lon,lat,z)
static inline double distance_meters2_3d(double lon0_deg, double lat0_deg, double z0_m,
                                         double lon1_deg, double lat1_deg, double z1_m)
{
    double horizontal_m = haversine_horizontal_meters(lon0_deg, lat0_deg, lon1_deg, lat1_deg);
    double dz = z1_m - z0_m;
    return horizontal_m * horizontal_m + dz * dz;
}

// Convert meter radius around a center (lon/lat in deg, z in m) to an AABB in degrees/meters
static inline SimpleBounds meters_radius_bbox_deg(const SimplePoint &center, double radius_m)
{
    // Use average meters per degree at the center latitude
    double lat0_rad = (double)center.y * M_PI / 180.0;
    const double inv_m_per_deg_lat = 1.0 / 111320.0;
    const double inv_m_per_deg_lon = 1.0 / 111320.0;
    double coslat = std::cos(lat0_rad);
    double coslat_abs = std::fabs(coslat);
    if (coslat_abs < 0.000001) coslat_abs = 0.000001;
    double dlat_deg = radius_m * inv_m_per_deg_lat;
    double dlon_deg = radius_m * (inv_m_per_deg_lon / coslat_abs);
    return SimpleBounds((float)(center.x - dlon_deg), (float)(center.y - dlat_deg), (float)(center.z - radius_m),
                        (float)(center.x + dlon_deg), (float)(center.y + dlat_deg), (float)(center.z + radius_m));
}

// Minimum 3D distance (meters) from a (lon,lat,z) point to a lon/lat/z AABB
static inline double min_distance_meters_to_bbox(const SimplePoint &center, const SimpleBounds &b)
{
    // Clamp to bbox for lon/lat and z
    double clamped_lon = std::min(std::max((double)center.x, (double)b.min_x), (double)b.max_x);
    double clamped_lat = std::min(std::max((double)center.y, (double)b.min_y), (double)b.max_y);
    double horizontal_m = haversine_horizontal_meters((double)center.x, (double)center.y,
                                                      clamped_lon, clamped_lat);
    double dz = 0.0;
    if ((double)center.z < (double)b.min_z) dz = (double)b.min_z - (double)center.z;
    else if ((double)center.z > (double)b.max_z) dz = (double)center.z - (double)b.max_z;
    return std::sqrt(horizontal_m * horizontal_m + dz * dz);
}

#endif // TRACE_GEO_UTILS_H


