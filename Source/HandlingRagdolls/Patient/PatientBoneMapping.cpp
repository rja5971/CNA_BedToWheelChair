// Fill out your copyright notice in the Description page of Project Settings.

#include "PatientBoneMapping.h"

FName UPatientBoneMapping::GetBoneName(EPatientBoneRole Role) const
{
	for (const FBoneRoleEntry& Entry : Mappings)
	{
		if (Entry.Role == Role)
		{
			return Entry.BoneName;
		}
	}
	return NAME_None;
}

TArray<FName> UPatientBoneMapping::GetBoneNames(const TArray<EPatientBoneRole>& Roles) const
{
	TArray<FName> Result;
	Result.Reserve(Roles.Num());

	for (EPatientBoneRole Role : Roles)
	{
		FName Name = GetBoneName(Role);
		if (!Name.IsNone())
		{
			Result.Add(Name);
		}
	}
	return Result;
}

bool UPatientBoneMapping::GetRoleForBone(FName BoneName, EPatientBoneRole& OutRole) const
{
	for (const FBoneRoleEntry& Entry : Mappings)
	{
		if (Entry.BoneName == BoneName)
		{
			OutRole = Entry.Role;
			return true;
		}
	}
	return false;
}
