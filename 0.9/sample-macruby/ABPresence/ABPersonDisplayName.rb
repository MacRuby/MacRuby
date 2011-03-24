class ABPerson

  # Pull first and last name, organization, and record flags
  # If the entry is a company, display the organization name instead
  def displayName
    firstName = valueForProperty KABFirstNameProperty
    lastName = valueForProperty KABLastNameProperty
    companyName = valueForProperty KABOrganizationProperty
    flagsValue = valueForProperty KABPersonFlags

    flags = flagsValue ? flagsValue.intValue : 0
    if (flags & KABShowAsMask) == KABShowAsCompany
      return companyName if companyName and companyName.length > 0
    end
    
    lastNameFirst = (flags & KABNameOrderingMask) == KABLastNameFirst
    hasFirstName = firstName and firstName.length > 0
    hasLastName = lastName and lastName.length > 0
  
    if hasLastName and hasFirstName
      if lastNameFirst
        "#{lastName} #{firstname}"
      else
        "#{firstName} #{lastName}"
      end
    elsif hasLastName
      lastName
    elsif hasFirstName
      firstName
    else
      'n/a'
    end
  end
  
end