def pdfFromAttributedStringOfSize(attString, size)
  v = TextDisplayView.alloc.initWithPageSize size, attributedString:attString
  v.dataWithPDFInsideRect v.bounds
end