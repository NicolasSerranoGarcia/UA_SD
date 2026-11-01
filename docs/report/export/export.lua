-- Keep explicit page breaks portable between the editable and printable reports.
function Div(block)
  if not block.classes:includes("page-break") then
    return nil
  end
  if FORMAT:match("docx") then
    return pandoc.RawBlock("openxml",
      '<w:p><w:r><w:br w:type="page"/></w:r></w:p>')
  elseif FORMAT:match("latex") then
    return pandoc.RawBlock("latex", "\\clearpage")
  end
end

local function keepTogether(block)
  if FORMAT:match("latex") then
    return {
      pandoc.RawBlock("latex", "\\noindent\\begin{minipage}{\\linewidth}"),
      block,
      pandoc.RawBlock("latex", "\\end{minipage}\\par\\medskip")
    }
  end
end

-- The short wire examples and screenshot prompts should not split across pages.
function CodeBlock(block)
  return keepTogether(block)
end

function BlockQuote(block)
  return keepTogether(block)
end
