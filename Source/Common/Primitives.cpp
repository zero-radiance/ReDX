#include "Primitives.h"
#include "Math.h"

using namespace DirectX;

AABox::AABox(const XMFLOAT3& pMin, const XMFLOAT3& pMax)
	: m_pMin{pMin}
	, m_pMax{pMax} {}

AABox::AABox(const XMFLOAT3& pMin, const float (&dims)[3])
	: m_pMin{pMin}
	, m_pMax{pMin.x + dims[0],
			 pMin.y + dims[1],
			 pMin.z + dims[2]} {}

AABox::AABox(const size_t count, const XMFLOAT3* points) {
    // Start with an empty bounding box.
    const AABox aaBox = AABox::empty();
    XMVECTOR    pMin  = aaBox.boundingPoint(0);
    XMVECTOR    pMax  = aaBox.boundingPoint(1);
    // Gradually extend it until it contains all points.
    for (size_t i = 0; i < count; ++i) {
        const XMVECTOR p = XMLoadFloat3(&points[i]);
        pMin = XMVectorMin(p, pMin);
        pMax = XMVectorMax(p, pMax);
    }
    // Store the computed bounding points.
    XMStoreFloat3(&m_pMin, pMin);
    XMStoreFloat3(&m_pMax, pMax);
}

AABox::AABox(const size_t count, const uint32_t* indices, const XMFLOAT3* points) {
    // Start with an empty bounding box.
    const AABox aaBox = AABox::empty();
    XMVECTOR    pMin  = aaBox.boundingPoint(0);
    XMVECTOR    pMax  = aaBox.boundingPoint(1);
    // Gradually extend it until it contains all points.
    for (size_t t = 0; t < count; t += 3) {
        for (size_t v = 0; v < 3; ++v) {
            const size_t   i = indices[t + v];
            const XMVECTOR p = XMLoadFloat3(&points[i]);
            pMin = XMVectorMin(p, pMin);
            pMax = XMVectorMax(p, pMax);
        }
    }
    // Store the computed bounding points.
    XMStoreFloat3(&m_pMin, pMin);
    XMStoreFloat3(&m_pMax, pMax);
}

AABox AABox::empty() {
    const XMFLOAT3 pMin = { FLT_MAX,  FLT_MAX,  FLT_MAX};
    const XMFLOAT3 pMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    return AABox{pMin, pMax};
}

void AABox::extend(const XMFLOAT3& point) {
    extend(XMLoadFloat3(&point));
}

void AABox::extend(FXMVECTOR point) {
    const XMVECTOR pMin = XMVectorMin(point, boundingPoint(0));
    const XMVECTOR pMax = XMVectorMax(point, boundingPoint(1));
    XMStoreFloat3(&m_pMin, pMin);
    XMStoreFloat3(&m_pMax, pMax);
}

XMVECTOR AABox::boundingPoint(const size_t index) const {
    assert(index <= 1);
    return SSE4::XMVectorSetW(XMLoadFloat3(&m_pMin + index), 1.f);
}

XMVECTOR AABox::center() const {
    return 0.5f * (boundingPoint(0) + boundingPoint(1));
}

Sphere::Sphere(const XMFLOAT3& center, const float radius)
    : m_data{center.x, center.y, center.z, radius} {}

Sphere::Sphere(FXMVECTOR center, FXMVECTOR radius) {
    const XMVECTOR data = SSE4::XMVectorSetW(center, XMVectorGetX(radius));
    XMStoreFloat4A(&m_data, data);
}

Sphere Sphere::inscribed(const AABox& aaBox) {
    // Compute the center and the radius.
    const XMVECTOR pMin     = aaBox.boundingPoint(0);
    const XMVECTOR pMax     = aaBox.boundingPoint(1);
    const XMVECTOR center   = 0.5f * (pMin + pMax);
    const XMVECTOR diagonal = pMax - pMin;
    const XMVECTOR radius   = XMVector4Min(0.5f * diagonal);
    // Create the sphere.
    return Sphere{center, radius};
}

Sphere Sphere::encompassing(const AABox& aaBox) {
    // Compute the center and the radius.
    const XMVECTOR pMin     = aaBox.boundingPoint(0);
    const XMVECTOR pMax     = aaBox.boundingPoint(1);
    const XMVECTOR center   = 0.5f * (pMin + pMax);
    const XMVECTOR diagonal = pMax - pMin;
    const XMVECTOR radius   = SSE4::XMVector3Length(0.5f * diagonal);
    // Create the sphere.
    return Sphere{center, radius};
}

XMVECTOR Sphere::center() const {
	return SSE4::XMVectorSetW(XMLoadFloat4A(&m_data), 1.f);
}

XMVECTOR Sphere::radius() const {
    return XMVectorSplatW(XMLoadFloat4A(&m_data));
}

bool Frustum::intersects(const AABox& aaBox) const {
    const XMVECTOR pMin = aaBox.boundingPoint(0);
    const XMVECTOR pMax = aaBox.boundingPoint(1);

    // Test against the left/right/top/bottom planes.
    const XMMATRIX tPlanes = XMLoadFloat4x4A(&m_tPlanes);
    // Find 4 farthest points along plane normals, transposed.
    XMVECTOR tFarthestPointsAlongNormal[3];
    // Iterate over X, Y, Z components.
    {
        XMVECTOR pMinSplatC, pMaxSplatC, normalComponentSign;
        // Splat the X component of 'pMin' and 'pMax'.
        pMinSplatC = XMVectorSplatX(pMin);
        pMaxSplatC = XMVectorSplatX(pMax);
        // Determine the sign of the X component of all 4 plane normals.
        normalComponentSign = XMVectorGreaterOrEqual(tPlanes.r[0], g_XMZero);
        // Select the component of 'pMax' if the sign is positive, of 'pMin' otherwise.
        tFarthestPointsAlongNormal[0] = XMVectorSelect(pMinSplatC, pMaxSplatC, normalComponentSign);
        // Splat the Y component of 'pMin' and 'pMax'.
        pMinSplatC = XMVectorSplatY(pMin);
        pMaxSplatC = XMVectorSplatY(pMax);
        // Determine the sign of the Y component of all 4 plane normals.
        normalComponentSign = XMVectorGreaterOrEqual(tPlanes.r[1], g_XMZero);
        // Select the component of 'pMax' if the sign is positive, of 'pMin' otherwise.
        tFarthestPointsAlongNormal[1] = XMVectorSelect(pMinSplatC, pMaxSplatC, normalComponentSign);
        // Splat the Z component of 'pMin' and 'pMax'.
        pMinSplatC = XMVectorSplatZ(pMin);
        pMaxSplatC = XMVectorSplatZ(pMax);
        // Determine the sign of the Z component of all 4 plane normals.
        normalComponentSign = XMVectorGreaterOrEqual(tPlanes.r[2], g_XMZero);
        // Select the component of 'pMax' if the sign is positive, of 'pMin' otherwise.
        tFarthestPointsAlongNormal[2] = XMVectorSelect(pMinSplatC, pMaxSplatC, normalComponentSign);
    }
    // Determine whether the 4 farthest points along plane normals
    // lie inside the positive half-spaces of their respective planes.
    // Compute the signed distances to the left/right/top/bottom frustum planes.
    const XMVECTOR upperPart = tPlanes.r[0] * tFarthestPointsAlongNormal[0]
                             + tPlanes.r[1] * tFarthestPointsAlongNormal[1];
    const XMVECTOR lowerPart = tPlanes.r[2] * tFarthestPointsAlongNormal[2]
                             + tPlanes.r[3];
    const XMVECTOR distances = upperPart + lowerPart;
    // Test the distances for being negative.
    const XMVECTOR outsideTests = XMVectorLess(distances, g_XMZero);
    // Check if at least one of the 'outside' tests passed.
    if (XMVector4NotEqualInt(outsideTests, XMVectorFalseInt())) {
        return false;
    }

    // Test whether the object is in front of the camera.
    // Our projection matrix is reversed, so we use the far plane.
    const XMVECTOR farPlane = XMLoadFloat4A(&m_farPlane);
    // First, we have to find the farthest point along the plane normal.
    const XMVECTOR normalComponentSign      = XMVectorGreaterOrEqual(farPlane, g_XMZero);
    const XMVECTOR farthestPointAlongNormal = XMVectorSelect(pMin, pMax, normalComponentSign);
    // Compute the signed distance to the far plane.
    const XMVECTOR distance = SSE4::XMVector4Dot(farPlane, farthestPointAlongNormal);
    // Test the distance for being negative.
    if (XMVectorGetIntX(XMVectorLess(distance, g_XMZero))) {
        return false;
    }
    return true;
}

bool Frustum::intersects(const Sphere& sphere) const {
    const XMVECTOR sphereCenter    =  sphere.center();
    const XMVECTOR negSphereRadius = -sphere.radius();

    // Test against the left/right/top/bottom planes.
	const XMMATRIX tPlanes  = XMLoadFloat4x4A(&m_tPlanes);
    // Compute the signed distances to the left/right/top/bottom frustum planes.
    const XMVECTOR upperPart = tPlanes.r[0] * XMVectorSplatX(sphereCenter)
                             + tPlanes.r[1] * XMVectorSplatY(sphereCenter);
    const XMVECTOR lowerPart = tPlanes.r[2] * XMVectorSplatZ(sphereCenter)
                             + tPlanes.r[3];
    const XMVECTOR distances = upperPart + lowerPart;
    // Test the distances against the (negated) radius of the bounding sphere.
    const XMVECTOR outsideTests = XMVectorLess(distances, negSphereRadius);
    // Check if at least one of the 'outside' tests passed.
    if (XMVector4NotEqualInt(outsideTests, XMVectorFalseInt())) {
        return false;
    }

    // Test whether the object is in front of the camera.
    // Our projection matrix is reversed, so we use the far plane.
	const XMVECTOR farPlane = XMLoadFloat4A(&m_farPlane);
    const XMVECTOR distance = SSE4::XMVector4Dot(farPlane, sphereCenter);
    // Test the distance against the (negated) radius of the bounding sphere.
    if (XMVectorGetIntX(XMVectorLess(distance, negSphereRadius))) {
        return false;
    }
    return true;
}
