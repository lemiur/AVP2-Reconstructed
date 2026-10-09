// Jupiter runtime/shared/src/motion.cpp (Talon version: one MotionState in/out structure,
// FLAG2_ORIENTMOVEMENT objects skip gravity and friction).
#include <math.h>
#include "motion.h"


// ----------------------------------------------------------------------- //
// Returns the vector that the physics would have the object move by.
// ----------------------------------------------------------------------- //
// Remaining diff: only the FLAG2_ORIENTMOVEMENT block (orig calls the _CVector ctor out of line for
// a * (timeIntegral * 0.5f), we inline it: VC6 inline budget), which shifts the rest; plus pObj/n
// register choice (edi/ebx swap) since the friction path stores m_Velocity without updating v.
// Wave 5: the original's orient block builds `a * dt` and `v * dt` with the _CVector(x,y,z) ctor inlined but
// `a * (timeIntegral * 0.5f)` through the out-of-line ctor 0x412960 (result in a stack temp that `dr +=` reads directly,
// so the source is probably `dr += a * (timeIntegral * 0.5f)`); the same early-inline/late-out-of-line pattern holds for
// the rest of the function (see the call lines of `build.py diff`).  inline_ballast `pending` at that statement
// (1..12 calls) and `cost` ballast (4..32) did not reproduce it; the missing budget accounting is unknown.
// Wave 6 phase 2: `dr += a * (timeIntegral * 0.5f)` directly compiles the same; `dr = v * dt + ...` and the orient
// block in an inline helper are worse (the helper refuses all three ctors). Ballast before the orient block: 7+ units
// make the orient ctor go out of line but also move the friction block's *=, /= out of line (worse); 3 free pending
// sites at the end do the same plus inline the last +=. No single position reproduces the exe's pattern.
// Wave 7 (tools/inline_budget.py CalcMotion --solve): the model agrees with our build on every site (B 2662u, own
// size 1327u). The orient block's third ctor is refused in the exe only if its share drops below 47u while the
// second's stays >= 47u: no budget alone does it; one more free pending site (an accessor) anywhere from the
// second a.Norm() on, plus 24-34u less own code (B -48..-68u), does. `LTVector &dr`/`float dt` locals instead of the
// macros are -35u; `LTVector &v, &a` -71u; WorldPoly::GetPlane() for m_pPlane +2u (none matched alone).
// Wave 7 phase 2: ALIGNED 236 (190 ignoring stack offsets). Audit: the only difference is the orient block's
// out-of-line LTVector(x,y,z) ctor (0x45c6ec, for `a * (timeIntegral * 0.5f)`), an inlining decision; the exe's
// extra 0xc4 immediate is its `lea eax,[edi+0xc4]` for &pObj->m_Velocity. Re-checked with --variants: `LTVector
// &dr = pState->m_Offset` and `float dt` locals (alone or together) leave no budget that reproduces the exe (the
// friction block's -= goes out of line first). The missing pending site after the second Norm() has no
// candidate in Jupiter's code (it writes the same statements); look for a Talon-only accessor or helper there.
// PARKED: one out-of-line ctor (inlining decision); the R11 model (ctor 47u, no tail site) still needs one more pending site after the 2nd Norm plus 24-34u less own code
// STUB: LITHTECH 0x0045c600
LTBOOL CalcMotion(MotionState *pState)
{
	LTVector velocityDelta, accelDelta;
	LTVector q, slopeVel, slopeAccel, vel;
	const LTVector *n;
	LTVector objectNormal, vTemp, vTemp2;
	float fExp;
	float timeIntegral;
	float velocityMagSqr, accelMagSqr;
	LTBOOL bFriction;

	LTObject *pObj = pState->m_pObj;

	// Talon keeps everything in the state structure.
	#define v	(*pState->m_pVelocity)
	#define a	(*pState->m_pAcceleration)
	#define dr	(pState->m_Offset)
	#define dt	(pState->m_dt)

	timeIntegral = dt * dt * 0.5f;
	velocityMagSqr = v.MagSqr();
	accelMagSqr = a.MagSqr();

	if(pObj->m_Flags2 & FLAG2_ORIENTMOVEMENT)
	{
		velocityDelta = a * dt;
		dr = v * dt;
		vTemp = a * (timeIntegral * 0.5f);
		dr += vTemp;

		pObj->m_Velocity += velocityDelta;
		v = pObj->m_Velocity;
		return LTTRUE;
	}

	LTVector vForce = pState->m_Info.m_Force;
	LTVector vUnitForce = pState->m_Info.m_UnitForce;

	bFriction = LTFALSE;
	n = LTNULL;

	// Stop objects that are moving very slowly...
	if(velocityMagSqr < 0.1f)
	{
		v.Init();
		velocityMagSqr = 0;
	}

	if(accelMagSqr < 0.1f)
	{
		a.Init();
		accelMagSqr = 0;
	}

	// Zero out the displacement to start with.
	dr.Init();

	// Update objects affected by gravity...
	if(pState->m_Flags & FLAG_GRAVITY)
	{
		// Add friction to objects standing on something...
		if(pObj->m_pStandingOn)
		{
			// Try to disable their physics.
			if(velocityMagSqr < 0.1f && accelMagSqr < 0.1f)
			{
				goto DisablePhysics;
			}
			else
			{
				// Check if object on world geometry...
				if(pObj->m_pNodeStandingOn)
				{
					// Calculate vector parallel to plane...
					WorldPoly *pPoly = pObj->m_pNodeStandingOn->m_pPoly;
					if(pPoly && pPoly->m_pSurface)
						n = &pPoly->m_pPlane->m_Normal;
				}
				else
				{
					// Object standing on another object, so assume opposite to gravity...
					n = &objectNormal;
					objectNormal = -vUnitForce;
				}

				// Calculate the acceleration including the force
				LTVector vAccelWithForce = vForce + a;

				// If we're on a slope that's at enough of an angle, allow it to slide
				LTVector vForceDir = vForce;
				vForceDir.Norm();
				if(vForceDir.Dot(*n) > pState->m_Info.m_SlideRatio)
				{
					float fAccelMag = vAccelWithForce.Mag();
					a = vAccelWithForce - *n * n->Dot(vAccelWithForce);

					// Don't allow it to accelerate up the slope..
					float fAccelDotForce = a.Dot(vForceDir);
					if(fAccelDotForce < 0.0f)
					{
						a -= vForceDir * fAccelDotForce;
					}

					a.Norm(fAccelMag);
				}
				else
				{
					// Figure out what our new velocity would be if only the force was used (i.e. are they jumping?)
					LTVector vNewVel = v + vForce * dt;
					// If we're going to be moving away from the plane, use the full force
					if(vNewVel.Dot(*n) > 0.01f)
					{
						a = vAccelWithForce;
					}
					// If the acceleration without the force isn't moving into the surface, project it there
					else if(a.Dot(*n) > 0.01f)
					{
						float fAccelMag = a.Mag();
						a -= *n * (n->Dot(a) + 1.0f);
						a.Norm(fAccelMag);

						bFriction = LTTRUE;
					}
					else
					{
						bFriction = LTTRUE;
					}
				}
			}
		}
		// Otherwise just apply gravity
		else
		{
			a += vForce;
		}
	}
	// If there's no gravity and they aren't moving, then disable their physics
	else if(velocityMagSqr < 0.1f && accelMagSqr < 0.1f)
	{
DisablePhysics:
		pObj->m_Velocity.Init();
		pObj->m_Acceleration.Init();
		pObj->m_InternalFlags &= ~IFLAG_APPLYPHYSICS;
		return LTFALSE;
	}

	// If friction
	// new velocity is given by:		v = ( a / k ) + ( v_0 - a / k ) * exp( -k * t )
	// new position is given by:		x = x_0 + ( a / k ) * t + ( k * v_0 - a ) * ( 1 - exp( -k * t )) / k^2
	if(bFriction && pObj->m_FrictionCoefficient > 0.0f)
	{
		// Velocity...
		fExp = (float)exp(-pObj->m_FrictionCoefficient * dt);
		vTemp = a / pObj->m_FrictionCoefficient;
		vTemp2 = v - vTemp;
		vTemp2 *= fExp;
		vel = vTemp2 + vTemp;

		// Position delta...
		dr = vTemp * dt;
		vTemp = v * pObj->m_FrictionCoefficient;
		vTemp -= a;
		vTemp *= ((1.0f - fExp) / pObj->m_FrictionCoefficient / pObj->m_FrictionCoefficient);
		dr += vTemp;
		pObj->m_Velocity = vel;
		return LTTRUE;
	}
	// If no friction
	// new velocity is given by:	v = v_0 + a * t
	// new position is given by:	x = x_0 + v_0 * t + .5 * a * t^2
	else
	{
		// Find the change in velocity...
		velocityDelta = a * dt;

		// Position delta...
		dr = v * dt;

		vTemp = a * (timeIntegral * 0.5f);
		dr += vTemp;

		// Add the final velocity to the new velocity.
		pObj->m_Velocity += velocityDelta;
		v = pObj->m_Velocity;
	}

	return LTTRUE;

	#undef v
	#undef a
	#undef dr
	#undef dt
}
